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
#include "push_proto.h"

static const char *TAG = "serialpush";

#define STALL_US (5 * 1000 * 1000)

typedef struct {
  uint8_t *blob;
  size_t len;
  uint32_t nonce; /* echoed in the verdict reply */
} frame;

static QueueHandle_t s_frames; /* depth 1: at most one push in flight */

/* Scanner state, owned by the task. The wire format constants and header
 * decode come from push_proto.h, shared with the TCP receiver; only the
 * resync behavior is this transport's own (see the note there). */
static struct {
  size_t header_got; /* 0..MPSH_HEADER_LEN: magic + little-endian length */
  uint8_t header[MPSH_HEADER_LEN];
  uint8_t *blob;
  size_t blob_len, blob_got;
  uint32_t nonce;
  int64_t last_progress_us;
} s;

static void reset_scanner(void) {
  free(s.blob);
  memset(&s, 0, sizeof s);
}

/* Feeds one byte; returns a completed frame's blob or NULL, filling both
 * outputs on completion. The nonce leaves through the out-parameter rather
 * than being read from scanner state by the caller — the old shape worked
 * only because the last body byte completes the frame before reset, and any
 * reordering would have silently sent wrong nonces. */
static uint8_t *feed(uint8_t b, size_t *len_out, uint32_t *nonce_out) {
  static const char magic[MPSH_MAGIC_LEN + 1] = MPSH_MAGIC;
  s.last_progress_us = esp_timer_get_time();

  if (s.header_got < MPSH_HEADER_LEN) {
    if (s.header_got < MPSH_MAGIC_LEN && b != (uint8_t)magic[s.header_got]) {
      /* Not the frame we hoped for. This byte could still start a new one. */
      s.header_got = 0;
      if (b == (uint8_t)magic[0]) s.header[s.header_got++] = b;
      return NULL;
    }
    s.header[s.header_got++] = b;
    if (s.header_got < MPSH_HEADER_LEN) return NULL;

    s.blob_len = mpsh_header_len(s.header);
    s.nonce = mpsh_header_nonce(s.header);
    if (!mpsh_len_ok(s.blob_len)) {
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
  *nonce_out = s.nonce;
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
      uint32_t nonce = 0;
      uint8_t *blob = feed(chunk[i], &len, &nonce);
      if (!blob) continue;
      frame f = {blob, len, nonce};
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

uint8_t *serialpush_poll(size_t *len_out, uint32_t *nonce_out) {
  frame f;
  if (!s_frames || xQueueReceive(s_frames, &f, 0) != pdTRUE) return NULL;
  *len_out = f.len;
  *nonce_out = f.nonce;
  return f.blob;
}

void serialpush_respond(uint32_t nonce, bool ok) {
  if (!s_frames) return; /* transport never came up */
  /* Through the driver's interrupt-driven TX, not the console's polling
   * path: the console's secondary writer drops output under back-pressure,
   * and a dropped verdict makes the sender re-push a program that already
   * landed. A binary reply carrying the sender's own nonce also cannot be
   * forged by log lines or a program's print() — which text acks were,
   * three separate times. */
  uint8_t reply[MPSH_REPLY_LEN];
  mpsh_make_reply(reply, ok, nonce);
  /* Sent three times: the secondary console writer shares this peripheral
   * without the driver (esp_vfs never switched to it), so a log line can
   * interleave into the middle of one reply and break the sender's 8-byte
   * match. Three spaced copies make an all-copies-corrupted race
   * vanishingly unlikely; the nonce makes duplicates harmless. Needs
   * on-board confirmation once hardware is back. */
  for (int i = 0; i < 3; i++) {
    usb_serial_jtag_write_bytes(reply, sizeof reply, pdMS_TO_TICKS(250));
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
