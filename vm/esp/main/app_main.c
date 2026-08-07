/* moth VM on ESP32: runs the embedded .mothb against real peripherals.
 *
 * Natives both drive the hardware and log, so a demo is legible over serial
 * whether or not anything is wired to the pins.
 */
#include "moth_vm.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "moth";

extern const uint8_t program_start[] asm("_binary_program_mothb_start");
extern const uint8_t program_end[] asm("_binary_program_mothb_end");

#define PWM_CHANNELS 6 /* LEDC_TIMER_0 fans out to these; tone owns TIMER_1 */
#define I2C_DEVICE_CACHE 8

static adc_oneshot_unit_handle_t s_adc1;
static i2c_master_bus_handle_t s_i2c_bus;
static struct {
  uint8_t addr;
  i2c_master_dev_handle_t dev;
} s_i2c_cache[I2C_DEVICE_CACHE];
static int s_i2c_cached;
static int s_pwm_pin[PWM_CHANNELS];
static int s_pwm_used;
static uint64_t s_rng = 1;

/* ---- output / timing --------------------------------------------------- */

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
  if (argv[0].type == MV_INT && argv[0].as.i > 0) vTaskDelay(pdMS_TO_TICKS(argv[0].as.i));
  return moth_null();
}

static moth_value n_delay_us(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type == MV_INT && argv[0].as.i > 0) esp_rom_delay_us((uint32_t)argv[0].as.i);
  return moth_null();
}

static moth_value n_millis(int c, const moth_value *v, void *u) {
  (void)c; (void)v; (void)u;
  return moth_int(esp_timer_get_time() / 1000);
}
static moth_value n_micros(int c, const moth_value *v, void *u) {
  (void)c; (void)v; (void)u;
  return moth_int(esp_timer_get_time());
}

/* ---- digital I/O ------------------------------------------------------- */

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

/* ---- analog ------------------------------------------------------------ */

static moth_value n_analog_read(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_int(-1);
  adc_channel_t channel;
  adc_unit_t unit;
  if (adc_oneshot_io_to_channel((int)argv[0].as.i, &unit, &channel) != ESP_OK ||
      unit != ADC_UNIT_1) {
    ESP_LOGW(TAG, "pin %d has no ADC1 channel", (int)argv[0].as.i);
    return moth_int(-1);
  }
  if (!s_adc1) {
    adc_oneshot_unit_init_cfg_t init = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&init, &s_adc1) != ESP_OK) return moth_int(-1);
  }
  adc_oneshot_chan_cfg_t chan = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
  adc_oneshot_config_channel(s_adc1, channel, &chan);
  int raw = -1;
  adc_oneshot_read(s_adc1, channel, &raw);
  return moth_int(raw);
}

/* LEDC channel per pin, allocated on first use. */
static int pwm_channel_for(int pin) {
  for (int i = 0; i < s_pwm_used; i++) {
    if (s_pwm_pin[i] == pin) return i;
  }
  if (s_pwm_used >= PWM_CHANNELS) return -1;
  s_pwm_pin[s_pwm_used] = pin;
  return s_pwm_used++;
}

static moth_value n_analog_write(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_null();
  int pin = (int)argv[0].as.i;
  int duty = (int)argv[1].as.i;
  if (duty < 0) duty = 0;
  if (duty > 255) duty = 255;

  static bool timer_ready;
  if (!timer_ready) {
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&t) != ESP_OK) return moth_null();
    timer_ready = true;
  }
  int ch = pwm_channel_for(pin);
  if (ch < 0) {
    ESP_LOGW(TAG, "no PWM channels left for pin %d", pin);
    return moth_null();
  }
  ledc_channel_config_t c = {
      .gpio_num = pin,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = (ledc_channel_t)ch,
      .timer_sel = LEDC_TIMER_0,
      .duty = duty,
      .hpoint = 0,
  };
  ledc_channel_config(&c);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
  ESP_LOGI(TAG, "pin %d PWM duty %d/255", pin, duty);
  return moth_null();
}

static moth_value n_tone(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_null();
  int pin = (int)argv[0].as.i;
  int freq = (int)argv[1].as.i;
  if (freq <= 0) return moth_null();

  ledc_timer_config_t t = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_1,
      .freq_hz = freq,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  if (ledc_timer_config(&t) != ESP_OK) return moth_null();
  int ch = pwm_channel_for(pin);
  if (ch < 0) return moth_null();
  ledc_channel_config_t c = {
      .gpio_num = pin,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = (ledc_channel_t)ch,
      .timer_sel = LEDC_TIMER_1,
      .duty = 128, /* square wave */
      .hpoint = 0,
  };
  ledc_channel_config(&c);
  ESP_LOGI(TAG, "pin %d tone %d Hz", pin, freq);
  return moth_null();
}

static moth_value n_no_tone(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_null();
  int ch = pwm_channel_for((int)argv[0].as.i);
  if (ch >= 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
  }
  ESP_LOGI(TAG, "pin %d tone off", (int)argv[0].as.i);
  return moth_null();
}

/* ---- random ------------------------------------------------------------ */

static moth_value n_random_seed(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type == MV_INT) s_rng = (uint64_t)argv[0].as.i | 1;
  return moth_null();
}

static moth_value n_random(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[0].as.i <= 0) return moth_int(0);
  uint64_t x = s_rng;
  x ^= x << 13; x ^= x >> 7; x ^= x << 17;
  s_rng = x;
  return moth_int((int64_t)(x % (uint64_t)argv[0].as.i));
}

/* ---- I2C --------------------------------------------------------------- */

static i2c_master_dev_handle_t i2c_device(uint8_t addr) {
  for (int i = 0; i < s_i2c_cached; i++) {
    if (s_i2c_cache[i].addr == addr) return s_i2c_cache[i].dev;
  }
  if (!s_i2c_bus || s_i2c_cached >= I2C_DEVICE_CACHE) return NULL;
  i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = addr,
      .scl_speed_hz = 100000,
  };
  i2c_master_dev_handle_t dev = NULL;
  if (i2c_master_bus_add_device(s_i2c_bus, &cfg, &dev) != ESP_OK) return NULL;
  s_i2c_cache[s_i2c_cached].addr = addr;
  s_i2c_cache[s_i2c_cached].dev = dev;
  s_i2c_cached++;
  return dev;
}

static moth_value n_i2c_begin(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_null();
  if (s_i2c_bus) return moth_null();
  i2c_master_bus_config_t cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .sda_io_num = (int)argv[0].as.i,
      .scl_io_num = (int)argv[1].as.i,
      .i2c_port = -1, /* auto */
      .flags.enable_internal_pullup = true,
  };
  if (i2c_new_master_bus(&cfg, &s_i2c_bus) != ESP_OK) {
    ESP_LOGE(TAG, "i2cBegin failed");
    return moth_null();
  }
  ESP_LOGI(TAG, "i2c started on sda %d, scl %d", (int)argv[0].as.i, (int)argv[1].as.i);
  return moth_null();
}

static moth_value n_i2c_ping(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (!s_i2c_bus || argv[0].type != MV_INT) return moth_bool(false);
  return moth_bool(i2c_master_probe(s_i2c_bus, (uint16_t)argv[0].as.i, 50) == ESP_OK);
}

static moth_value n_i2c_write_reg(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT || argv[2].type != MV_INT) {
    return moth_bool(false);
  }
  i2c_master_dev_handle_t dev = i2c_device((uint8_t)argv[0].as.i);
  if (!dev) return moth_bool(false);
  uint8_t buf[2] = {(uint8_t)argv[1].as.i, (uint8_t)argv[2].as.i};
  return moth_bool(i2c_master_transmit(dev, buf, 2, 100) == ESP_OK);
}

static moth_value n_i2c_read_reg(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_int(-1);
  i2c_master_dev_handle_t dev = i2c_device((uint8_t)argv[0].as.i);
  if (!dev) return moth_int(-1);
  uint8_t reg = (uint8_t)argv[1].as.i, out = 0;
  if (i2c_master_transmit_receive(dev, &reg, 1, &out, 1, 100) != ESP_OK) return moth_int(-1);
  return moth_int(out);
}

/* ---- UART -------------------------------------------------------------- */

static moth_value n_uart_begin(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  for (int i = 0; i < 4; i++) {
    if (argv[i].type != MV_INT) return moth_null();
  }
  uart_port_t port = (uart_port_t)argv[0].as.i;
  uart_config_t cfg = {
      .baud_rate = (int)argv[3].as.i,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  if (uart_driver_install(port, 256, 0, 0, NULL, 0) != ESP_OK) return moth_null();
  uart_param_config(port, &cfg);
  uart_set_pin(port, (int)argv[1].as.i, (int)argv[2].as.i, UART_PIN_NO_CHANGE,
               UART_PIN_NO_CHANGE);
  ESP_LOGI(TAG, "uart %d open: tx %d, rx %d, %d baud", (int)port, (int)argv[1].as.i,
           (int)argv[2].as.i, (int)argv[3].as.i);
  return moth_null();
}

static moth_value n_uart_write(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_null();
  uint8_t byte = (uint8_t)argv[1].as.i;
  uart_write_bytes((uart_port_t)argv[0].as.i, &byte, 1);
  return moth_null();
}

static moth_value n_uart_available(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_int(0);
  size_t n = 0;
  uart_get_buffered_data_len((uart_port_t)argv[0].as.i, &n);
  return moth_int((int64_t)n);
}

static moth_value n_uart_read(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_int(-1);
  uint8_t byte = 0;
  int n = uart_read_bytes((uart_port_t)argv[0].as.i, &byte, 1, 0);
  return moth_int(n == 1 ? byte : -1);
}

/* ---- entry ------------------------------------------------------------- */

static void register_all(moth_vm *vm) {
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "delayMicroseconds", n_delay_us, NULL);
  moth_register(vm, "millis", n_millis, NULL);
  moth_register(vm, "micros", n_micros, NULL);
  moth_register(vm, "pinOutput", n_pin_output, NULL);
  moth_register(vm, "pinInput", n_pin_input, NULL);
  moth_register(vm, "pinInputPullup", n_pin_input_pullup, NULL);
  moth_register(vm, "digitalWrite", n_digital_write, NULL);
  moth_register(vm, "digitalRead", n_digital_read, NULL);
  moth_register(vm, "analogRead", n_analog_read, NULL);
  moth_register(vm, "analogWrite", n_analog_write, NULL);
  moth_register(vm, "tone", n_tone, NULL);
  moth_register(vm, "noTone", n_no_tone, NULL);
  moth_register(vm, "randomSeed", n_random_seed, NULL);
  moth_register(vm, "random", n_random, NULL);
  moth_register(vm, "i2cBegin", n_i2c_begin, NULL);
  moth_register(vm, "i2cPing", n_i2c_ping, NULL);
  moth_register(vm, "i2cWriteReg", n_i2c_write_reg, NULL);
  moth_register(vm, "i2cReadReg", n_i2c_read_reg, NULL);
  moth_register(vm, "uartBegin", n_uart_begin, NULL);
  moth_register(vm, "uartWrite", n_uart_write, NULL);
  moth_register(vm, "uartAvailable", n_uart_available, NULL);
  moth_register(vm, "uartRead", n_uart_read, NULL);
}

void app_main(void) {
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
