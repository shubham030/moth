/* moth VM on ESP32: runs the embedded .mothb with real peripherals.
 *
 * Natives both drive the hardware and log, so the demo is legible over serial
 * whether or not an LED happens to be wired to the pin.
 */
#include "moth_vm.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "moth";

extern const uint8_t program_start[] asm("_binary_program_mothb_start");
extern const uint8_t program_end[] asm("_binary_program_mothb_end");

static moth_value n_print(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  switch (argv[0].type) {
    case MV_INT: ESP_LOGI(TAG, "%" PRId64, argv[0].as.i); break;
    case MV_DOUBLE: ESP_LOGI(TAG, "%g", argv[0].as.d); break;
    case MV_BOOL: ESP_LOGI(TAG, "%s", argv[0].as.b ? "true" : "false"); break;
    case MV_NULL: ESP_LOGI(TAG, "null"); break;
  }
  return moth_null();
}

static moth_value n_delay(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type == MV_INT && argv[0].as.i > 0) {
    vTaskDelay(pdMS_TO_TICKS(argv[0].as.i));
  }
  return moth_null();
}

static moth_value n_millis(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)argv; (void)user;
  return moth_int(esp_timer_get_time() / 1000);
}

static moth_value pin_mode(const moth_value *argv, gpio_mode_t mode, bool pullup) {
  if (argv[0].type != MV_INT) return moth_null();
  int pin = (int)argv[0].as.i;
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << pin,
      .mode = mode,
      .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
  };
  gpio_config(&cfg);
  ESP_LOGI(TAG, "pin %d -> %s", pin,
           mode == GPIO_MODE_OUTPUT ? "output" : (pullup ? "input (pull-up)" : "input"));
  return moth_null();
}

static moth_value n_pin_output(int c, const moth_value *v, void *u) {
  (void)c; (void)u; return pin_mode(v, GPIO_MODE_OUTPUT, false);
}
static moth_value n_pin_input(int c, const moth_value *v, void *u) {
  (void)c; (void)u; return pin_mode(v, GPIO_MODE_INPUT, false);
}
static moth_value n_pin_input_pullup(int c, const moth_value *v, void *u) {
  (void)c; (void)u; return pin_mode(v, GPIO_MODE_INPUT, true);
}

static moth_value n_digital_write(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_BOOL) return moth_null();
  gpio_set_level((gpio_num_t)argv[0].as.i, argv[1].as.b);
  ESP_LOGI(TAG, "pin %d = %s", (int)argv[0].as.i, argv[1].as.b ? "HIGH" : "low");
  return moth_null();
}

static moth_value n_digital_read(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_bool(false);
  return moth_bool(gpio_get_level((gpio_num_t)argv[0].as.i) != 0);
}

void app_main(void) {
  moth_vm *vm = moth_new();
  if (!vm) {
    ESP_LOGE(TAG, "out of memory");
    return;
  }
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "millis", n_millis, NULL);
  moth_register(vm, "pinOutput", n_pin_output, NULL);
  moth_register(vm, "pinInput", n_pin_input, NULL);
  moth_register(vm, "pinInputPullup", n_pin_input_pullup, NULL);
  moth_register(vm, "digitalWrite", n_digital_write, NULL);
  moth_register(vm, "digitalRead", n_digital_read, NULL);

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
