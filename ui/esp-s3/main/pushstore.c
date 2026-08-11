/* See pushstore.h for the contract. */
#include "pushstore.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "nvs.h"

static const char *TAG = "pushstore";

/* "MPSB": MPSH is the wire protocol, this is the at-rest format — different
 * magic so a half-written wire capture can never look like a stored blob. */
#define STORE_MAGIC 0x4253504Du /* 'MPSB' little-endian */

typedef struct {
  uint32_t magic;
  uint32_t len;
  uint32_t crc; /* crc32 of the blob bytes */
  uint32_t reserved;
} store_header;

static const esp_partition_t *part(void) {
  static const esp_partition_t *p;
  if (!p) {
    p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "mothb");
    if (!p) ESP_LOGW(TAG, "no mothb partition — pushes will not persist");
  }
  return p;
}

bool pushstore_save(const uint8_t *blob, size_t len) {
  const esp_partition_t *p = part();
  if (!p || len == 0 || len + sizeof(store_header) > p->size) return false;

  /* Erase must cover header + blob, rounded up to the 4KB sector. */
  const size_t total = sizeof(store_header) + len;
  const size_t sectors = (total + 4095) & ~(size_t)4095;
  if (esp_partition_erase_range(p, 0, sectors) != ESP_OK) return false;

  store_header h = {STORE_MAGIC, (uint32_t)len,
                    esp_rom_crc32_le(0, blob, len), 0};
  if (esp_partition_write(p, 0, &h, sizeof h) != ESP_OK) return false;
  if (esp_partition_write(p, sizeof h, blob, len) != ESP_OK) return false;
  ESP_LOGI(TAG, "stored %u bytes; survives reboot", (unsigned)len);
  return true;
}

const uint8_t *pushstore_load(size_t *len_out) {
  const esp_partition_t *p = part();
  if (!p) return NULL;

  store_header h;
  if (esp_partition_read(p, 0, &h, sizeof h) != ESP_OK) return NULL;
  if (h.magic != STORE_MAGIC || h.len == 0 ||
      h.len + sizeof h > p->size) return NULL;

  const void *mapped = NULL;
  esp_partition_mmap_handle_t handle; /* never unmapped: the VM runs from it */
  if (esp_partition_mmap(p, 0, sizeof h + h.len, ESP_PARTITION_MMAP_DATA,
                         &mapped, &handle) != ESP_OK) {
    return NULL;
  }
  const uint8_t *blob = (const uint8_t *)mapped + sizeof h;
  if (esp_rom_crc32_le(0, blob, h.len) != h.crc) {
    ESP_LOGW(TAG, "stored blob fails its CRC — ignoring it");
    esp_partition_munmap(handle);
    return NULL;
  }
  *len_out = h.len;
  return blob;
}

void pushstore_invalidate(void) {
  const esp_partition_t *p = part();
  /* One erased sector kills the header; the stale blob bytes behind it are
   * unreachable without it. */
  if (p) esp_partition_erase_range(p, 0, 4096);
}

/* ---- strike counter, in NVS so it survives the reboot it exists to count */

static bool strikes_open(nvs_handle_t *h) {
  return nvs_open("moth", NVS_READWRITE, h) == ESP_OK;
}

int pushstore_strikes(void) {
  nvs_handle_t h;
  if (!strikes_open(&h)) return 0;
  int32_t n = 0;
  nvs_get_i32(h, "strikes", &n);
  nvs_close(h);
  return (int)n;
}

void pushstore_add_strike(void) {
  nvs_handle_t h;
  if (!strikes_open(&h)) return;
  int32_t n = 0;
  nvs_get_i32(h, "strikes", &n);
  nvs_set_i32(h, "strikes", n + 1);
  nvs_commit(h);
  nvs_close(h);
}

void pushstore_clear_strikes(void) {
  nvs_handle_t h;
  if (!strikes_open(&h)) return;
  nvs_set_i32(h, "strikes", 0);
  nvs_commit(h);
  nvs_close(h);
}
