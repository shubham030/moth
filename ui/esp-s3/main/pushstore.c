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
    if (!p) {
      /* The usual cause is a checkout from before the custom partition
       * table: .gitignore keeps the old generated sdkconfig, and it beats
       * sdkconfig.defaults, so the build silently keeps the single-app
       * layout. Loud, with the remedy, because "pushes don't survive
       * reboot" is otherwise a mystery. */
      ESP_LOGE(TAG, "no mothb partition — pushes will NOT persist. Stale "
                    "sdkconfig? run: rm ui/esp-s3/sdkconfig && idf.py "
                    "reconfigure, then flash");
    }
  }
  return p;
}

/* The live mapping, if any — kept so invalidate can unmap it. Three paths
 * invalidate after loading, and each used to leak the mapping of a region
 * that had just been erased underneath it. */
static esp_partition_mmap_handle_t s_map;
static bool s_mapped;

void pushstore_release(void) {
  if (s_mapped) {
    esp_partition_munmap(s_map);
    s_mapped = false;
  }
}

bool pushstore_save(const uint8_t *blob, size_t len) {
  const esp_partition_t *p = part();
  if (!p) return false;
  /* The boot-time mapping may still be live; erasing under it violates the
   * same invariant invalidate maintains. The callers have already torn down
   * anything executing from it. */
  pushstore_release();
  /* Any failure from here on must leave the store INVALID, not stale: the
   * caller is switching to the new program now, and "save failed" with the
   * old blob still valid means a reboot resurrects a program the user
   * already replaced — worse than losing the push. */
  if (len == 0 || len > p->size - sizeof(store_header)) {
    pushstore_invalidate();
    return false;
  }

  /* Erase must cover header + blob, rounded up to the 4KB sector. */
  const size_t total = sizeof(store_header) + len;
  const size_t sectors = (total + 4095) & ~(size_t)4095;
  if (esp_partition_erase_range(p, 0, sectors) != ESP_OK) {
    pushstore_invalidate(); /* the old header may have survived */
    return false;
  }

  /* Blob first, header last. The header's magic is what makes the store
   * valid, so a power cut mid-save must leave it unwritten — a torn save
   * that wrote the magic but not the length would read back as a "valid"
   * blob of erased-flash garbage. */
  store_header h = {STORE_MAGIC, (uint32_t)len,
                    esp_rom_crc32_le(0, blob, len), 0};
  if (esp_partition_write(p, sizeof h, blob, len) != ESP_OK) return false;
  if (esp_partition_write(p, 0, &h, sizeof h) != ESP_OK) return false;
  ESP_LOGI(TAG, "stored %u bytes; survives reboot", (unsigned)len);
  return true;
}

const uint8_t *pushstore_load(size_t *len_out) {
  const esp_partition_t *p = part();
  if (!p) return NULL;

  store_header h;
  if (esp_partition_read(p, 0, &h, sizeof h) != ESP_OK) return NULL;
  /* Bound by subtraction: h.len is attacker-and-erased-flash-controlled, and
   * `h.len + sizeof h` wraps for 0xFFFFFFFF — which is exactly what a torn
   * header reads as. A wrapped bound once passed this check and the CRC then
   * read 4GB off a 15-byte mapping; the crash sat before add_strike, so the
   * crash-loop guard never engaged and the board looped until reflashed. */
  if (h.magic != STORE_MAGIC || h.len == 0 ||
      h.len > p->size - sizeof h) return NULL;

  const void *mapped = NULL;
  if (esp_partition_mmap(p, 0, sizeof h + h.len, ESP_PARTITION_MMAP_DATA,
                         &mapped, &s_map) != ESP_OK) {
    return NULL;
  }
  s_mapped = true;
  const uint8_t *blob = (const uint8_t *)mapped + sizeof h;
  if (esp_rom_crc32_le(0, blob, h.len) != h.crc) {
    ESP_LOGW(TAG, "stored blob fails its CRC — ignoring it");
    pushstore_release();
    return NULL;
  }
  *len_out = h.len;
  return blob;
}

void pushstore_invalidate(void) {
  /* Unmap before erasing — nothing may still be executing from the mapping
   * (the callers tear the VM down first), and erasing under a live handle
   * leaks it. */
  pushstore_release();
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

/* Whether the previous boot was running a pushed program — written before
 * the program runs, read with esp_reset_reason() on the next boot so a
 * panic can be attributed to the right program. */
bool pushstore_boot_was_store(void) {
  nvs_handle_t h;
  if (!strikes_open(&h)) return false;
  int32_t v = 0;
  nvs_get_i32(h, "ran_store", &v);
  nvs_close(h);
  return v != 0;
}

void pushstore_set_boot_source(bool store) {
  nvs_handle_t h;
  if (!strikes_open(&h)) return;
  nvs_set_i32(h, "ran_store", store ? 1 : 0);
  nvs_commit(h);
  nvs_close(h);
}
