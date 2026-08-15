/* moth VM on ESP32: runs the embedded .mothb against real peripherals.
 *
 * The hardware natives live in hw_natives.c, shared with the display
 * firmware — the same program must behave the same on either. This file
 * keeps the pieces that differ by host: print, timing, and boot.
 */
#include "moth_vm.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw_natives.h"
#include "nvs_flash.h"

static const char *TAG = "moth";

extern const uint8_t program_start[] asm("_binary_program_mothb_start");
extern const uint8_t program_end[] asm("_binary_program_mothb_end");

static moth_value n_print(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  /* Same renderer the VM uses for '$x', so the two can never disagree. */
  moth_value text = moth_to_string(vm, argv[0]);
  int len = 0;
  const char *chars = moth_string_chars(text, &len);
  ESP_LOGI(TAG, "%.*s", len, chars ? chars : ""); /* not NUL-terminated */
  return moth_null();
}

static moth_value n_delay(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type == MV_INT && argv[0].as.i > 0) vTaskDelay(pdMS_TO_TICKS(argv[0].as.i));
  return moth_null();
}

static moth_value n_delay_us(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type == MV_INT && argv[0].as.i > 0) esp_rom_delay_us((uint32_t)argv[0].as.i);
  return moth_null();
}

static moth_value n_millis(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  return moth_int(esp_timer_get_time() / 1000);
}
static moth_value n_micros(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  return moth_int(esp_timer_get_time());
}

static void register_all(moth_vm *vm) {
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "delayMicroseconds", n_delay_us, NULL);
  moth_register(vm, "millis", n_millis, NULL);
  moth_register(vm, "micros", n_micros, NULL);
  moth_hw_register(vm);
}

void app_main(void) {
  /* prefs live in NVS; a fresh chip needs the partition initialized. */
  esp_err_t nv = nvs_flash_init();
  if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  moth_vm *vm = moth_new();
  if (!vm) {
    ESP_LOGE(TAG, "out of memory");
    return;
  }
  register_all(vm);

  size_t len = (size_t)(program_end - program_start);
  ESP_LOGI(TAG, "loading %u bytes of Dart bytecode", (unsigned)len);

  moth_status st = moth_load(vm, program_start, len);
  if (st != MOTH_OK) {
    ESP_LOGE(TAG, "load failed (%d): %s", st, moth_error(vm));
    return;
  }
  ESP_LOGI(TAG, "running Dart on the VM");

  st = moth_run(vm);
  ESP_LOGI(TAG, "program finished (%d): %s", st, st == MOTH_OK ? "ok" : moth_error(vm));
  moth_free(vm);
}
