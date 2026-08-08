/* moth on the ESP32-S3 with a display: the VM runs a Dart program, the same
 * moth_render code draws it, and the panel shows the result.
 *
 * The Dart program owns the loop, so the host does its work from the frame
 * hook inside uiCommit — exactly as mothsim does on the desktop.
 */
#include "moth_render.h"
#include "moth_ui.h"
#include "moth_vm.h"
#include "panel.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "moth";

extern const uint8_t program_start[] asm("_binary_program_mothb_start");
extern const uint8_t program_end[] asm("_binary_program_mothb_end");

static void on_frame(bool repainted, void *user) {
  (void)user;

  /* Touch is polled here rather than on a task, so events reach the VM
   * between its own loop iterations and never mid-instruction. */
  int x, y;
  bool down = panel_touch_read(&x, &y);
  static int last_x, last_y;
  if (down) {
    last_x = x;
    last_y = y;
  }
  mr_pointer(down ? x : last_x, down ? y : last_y, down);

  if (repainted) panel_present_argb(mr_framebuffer());
}

static moth_value n_print(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  moth_value text = moth_to_string(vm, argv[0]);
  int len = 0;
  const char *chars = moth_string_chars(text, &len);
  ESP_LOGI(TAG, "%.*s", len, chars ? chars : "");
  return moth_null();
}

static moth_value n_delay(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type == MV_INT && argv[0].as.i > 0) vTaskDelay(pdMS_TO_TICKS(argv[0].as.i));
  return moth_null();
}

static moth_value n_millis(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  return moth_int(esp_timer_get_time() / 1000);
}

void app_main(void) {
  ESP_ERROR_CHECK(panel_init());

  /* 1.75" CO5300 is circular: corners sit behind the bezel. */
  mr_config cfg = {PANEL_W, PANEL_H, MR_SHAPE_ROUND};
  if (!mr_init(&cfg)) {
    ESP_LOGE(TAG, "renderer init failed");
    return;
  }

  moth_vm *vm = moth_new();
  if (!vm) {
    ESP_LOGE(TAG, "out of memory");
    return;
  }
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "millis", n_millis, NULL);
  moth_ui_register(vm);
  moth_ui_set_frame_hook(on_frame, NULL);

  size_t len = (size_t)(program_end - program_start);
  ESP_LOGI(TAG, "loading %u bytes of Dart bytecode for a %dx%d display",
           (unsigned)len, PANEL_W, PANEL_H);

  moth_status st = moth_load(vm, program_start, len);
  if (st != MOTH_OK) {
    ESP_LOGE(TAG, "load failed (%d): %s", st, moth_error(vm));
    return;
  }
  ESP_LOGI(TAG, "running Dart UI");

  st = moth_run(vm);
  ESP_LOGI(TAG, "program finished (%d): %s", st, st == MOTH_OK ? "ok" : moth_error(vm));
  moth_free(vm);
}
