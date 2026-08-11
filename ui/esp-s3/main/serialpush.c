/* See serialpush.h for the contract.
 *
 * The receive side is a byte-at-a-time state machine rather than a header
 * read followed by a body read: console input is not a clean stream — a
 * terminal may have sent keystrokes, a previous push may have been cut off
 * mid-frame — so the scanner resynchronizes on every byte that breaks the
 * pattern, and a frame that stalls mid-body is abandoned the way the TCP
 * receiver abandons a stalled client.
 */
#include "serialpush.h"

#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "serialpush";

#define MAX_BLOB (1u << 20) /* matches the TCP receiver's limit */
#define STALL_US (5 * 1000 * 1000)

typedef struct {
  uint8_t *blob;
  size_t len;
} frame;

static QueueHandle_t s_frames; /* depth 1: at most one push in flight */

/* Scanner state, owned by the task. */
static struct {
  size_t header_got; /* 0..8: magic + little-endian length */
  uint8_t header[8];
  uint8_t *blob;
  size_t blob_len, blob_got;
  int64_t last_progress_us;
} s;

static void reset_scanner(void) {
  free(s.blob);
  memset(&s, 0, sizeof s);
}

/* Feeds one byte; returns a completed frame's blob or NULL. */
static uint8_t *feed(uint8_t b, size_t *len_out) {
  static const char magic[4] = {'M', 'P', 'S', 'H'};
  s.last_progress_us = esp_timer_get_time();

  if (s.header_got < 8) {
    if (s.header_got < 4 && b != (uint8_t)magic[s.header_got]) {
      /* Not the frame we hoped for. This byte could still start a new one. */
      s.header_got = 0;
      if (b == (uint8_t)magic[0]) s.header[s.header_got++] = b;
      return NULL;
    }
    s.header[s.header_got++] = b;
    if (s.header_got < 8) return NULL;

    s.blob_len = (size_t)s.header[4] | ((size_t)s.header[5] << 8) |
                 ((size_t)s.header[6] << 16) | ((size_t)s.header[7] << 24);
    if (s.blob_len == 0 || s.blob_len > MAX_BLOB) {
      reset_scanner();
      return NULL;
    }
    s.blob = malloc(s.blob_len);
    if (!s.blob) {
      reset_scanner();
      return NULL;
    }
    s.blob_got = 0;
    return NULL;
  }

  s.blob[s.blob_got++] = b;
  if (s.blob_got < s.blob_len) return NULL;

  uint8_t *done = s.blob;
  *len_out = s.blob_len;
  s.blob = NULL;
  reset_scanner();
  return done;
}

static void serialpush_task(void *arg) {
  (void)arg;
  uint8_t chunk[256];
  for (;;) {
    int n = usb_serial_jtag_read_bytes(chunk, sizeof chunk, pdMS_TO_TICKS(100));
    if (n <= 0) {
      /* Mid-frame and silent too long: the sender is gone, and a half frame
       * held forever would eat the start of the next push. */
      if ((s.header_got > 0 || s.blob) &&
          esp_timer_get_time() - s.last_progress_us > STALL_US) {
        ESP_LOGW(TAG, "stalled mid-frame; dropping it");
        reset_scanner();
      }
      continue;
    }
    for (int i = 0; i < n; i++) {
      size_t len = 0;
      uint8_t *blob = feed(chunk[i], &len);
      if (!blob) continue;
      frame f = {blob, len};
      if (xQueueSend(s_frames, &f, 0) != pdTRUE) {
        free(blob); /* one already waiting; the sender can retry */
      }
    }
  }
}

void serialpush_start(void) {
  usb_serial_jtag_driver_config_t cfg = {
      .rx_buffer_size = 4096,
      .tx_buffer_size = 256,
  };
  if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
    ESP_LOGW(TAG, "USB serial driver install failed — cable push disabled");
    return;
  }
  /* Boot-time heap pressure is real here — the panel's DMA buffers and the
   * framebuffer are already allocated — and the task would write to a NULL
   * queue on its first completed frame, mid-push. */
  s_frames = xQueueCreate(1, sizeof(frame));
  if (!s_frames) {
    ESP_LOGW(TAG, "out of memory for the push queue — cable push disabled");
    return;
  }
  /* Modest stack: the task moves bytes and calls malloc, nothing deep. */
  if (xTaskCreate(serialpush_task, "serialpush", 3072, NULL, 5, NULL) !=
      pdPASS) {
    ESP_LOGW(TAG, "out of memory for the push task — cable push disabled");
    vQueueDelete(s_frames);
    s_frames = NULL;
    return;
  }
  ESP_LOGI(TAG, "cable push ready: mothc app.dart --push <this serial port>");
}

uint8_t *serialpush_poll(size_t *len_out) {
  frame f;
  if (!s_frames || xQueueReceive(s_frames, &f, 0) != pdTRUE) return NULL;
  *len_out = f.len;
  return f.blob;
}
