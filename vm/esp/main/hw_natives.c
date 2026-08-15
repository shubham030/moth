/* See hw_natives.h. Bodies moved verbatim from vm/esp/main/app_main.c —
 * behavior is unchanged for everything that existed; the additions are
 * bulk I2C, prefs over NVS, and servo PWM. */
#include "hw_natives.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "moth";

#define PWM_CHANNELS 6 /* LEDC_TIMER_0 fans out to these; tone owns TIMER_1 */
#define SERVO_CHANNELS 2 /* channels 6..7 on TIMER_2 at 50Hz */
#define I2C_DEVICE_CACHE 8

static adc_oneshot_unit_handle_t s_adc1;
static i2c_master_bus_handle_t s_i2c_bus;
static bool s_i2c_adopted;
static struct {
  uint8_t addr;
  i2c_master_dev_handle_t dev;
} s_i2c_cache[I2C_DEVICE_CACHE];
static int s_i2c_cached;
static int s_pwm_pin[PWM_CHANNELS];
static int s_pwm_used;
static int s_servo_pin[SERVO_CHANNELS];
static int s_servo_used;
static uint64_t s_rng = 1;

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

static moth_value n_pin_output(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)u; return pin_mode(v, GPIO_MODE_OUTPUT, false);
}
static moth_value n_pin_input(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)u; return pin_mode(v, GPIO_MODE_INPUT, false);
}
static moth_value n_pin_input_pullup(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)u; return pin_mode(v, GPIO_MODE_INPUT, true);
}

static moth_value n_digital_write(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_BOOL) return moth_null();
  gpio_set_level((gpio_num_t)argv[0].as.i, argv[1].as.b);
  ESP_LOGI(TAG, "pin %d = %s", (int)argv[0].as.i, argv[1].as.b ? "HIGH" : "low");
  return moth_null();
}

static moth_value n_digital_read(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_bool(false);
  return moth_bool(gpio_get_level((gpio_num_t)argv[0].as.i) != 0);
}

/* ---- analog ------------------------------------------------------------ */

static moth_value n_analog_read(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
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

static moth_value n_analog_write(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
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

/* ---- sound ------------------------------------------------------------- */

static moth_value n_tone(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
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

static moth_value n_no_tone(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_null();
  int ch = pwm_channel_for((int)argv[0].as.i);
  if (ch >= 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
  }
  ESP_LOGI(TAG, "pin %d tone off", (int)argv[0].as.i);
  return moth_null();
}

/* ---- servo — 50Hz on its own timer, so analogWrite's 5kHz and tone's
 * frequency never fight it. Two channels; hobby boards rarely drive more,
 * and the pool is documented in hardware.md. ---------------------------- */

static int servo_channel_for(int pin) {
  for (int i = 0; i < s_servo_used; i++) {
    if (s_servo_pin[i] == pin) return PWM_CHANNELS + i;
  }
  if (s_servo_used >= SERVO_CHANNELS) return -1;
  s_servo_pin[s_servo_used] = pin;
  return PWM_CHANNELS + s_servo_used++;
}

static moth_value n_servo_attach(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_null();
  int pin = (int)argv[0].as.i;

  static bool timer_ready;
  if (!timer_ready) {
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        /* 14 bits at 50Hz: one step is ~1.2us of pulse width, fine enough
         * that a degree of servo travel spans several steps. */
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = LEDC_TIMER_2,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&t) != ESP_OK) {
      ESP_LOGE(TAG, "servo timer failed");
      return moth_null();
    }
    timer_ready = true;
  }
  int ch = servo_channel_for(pin);
  if (ch < 0) {
    ESP_LOGW(TAG, "no servo channels left for pin %d (max %d)", pin, SERVO_CHANNELS);
    return moth_null();
  }
  ledc_channel_config_t c = {
      .gpio_num = pin,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = (ledc_channel_t)ch,
      .timer_sel = LEDC_TIMER_2,
      .duty = 0, /* no pulse until the program commands a position */
      .hpoint = 0,
  };
  ledc_channel_config(&c);
  ESP_LOGI(TAG, "pin %d servo attached (50Hz)", pin);
  return moth_null();
}

static moth_value n_servo_us(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_null();
  int pin = (int)argv[0].as.i;
  int64_t us = argv[1].as.i;
  if (us < 500) us = 500;
  if (us > 2500) us = 2500;
  int ch = servo_channel_for(pin);
  if (ch < 0) return moth_null();
  /* duty = us / 20000us period, scaled to 14 bits. */
  uint32_t duty = (uint32_t)(us * 16384 / 20000);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
  ESP_LOGI(TAG, "pin %d servo %lldus", pin, (long long)us);
  return moth_null();
}

/* ---- random ------------------------------------------------------------ */

static moth_value n_random_seed(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type == MV_INT) s_rng = (uint64_t)argv[0].as.i | 1;
  return moth_null();
}

static moth_value n_random(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
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

void moth_hw_adopt_i2c_bus(i2c_master_bus_handle_t bus) {
  s_i2c_bus = bus;
  s_i2c_adopted = true;
}

static moth_value n_i2c_begin(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_null();
  if (s_i2c_bus) {
    if (s_i2c_adopted) {
      ESP_LOGI(TAG, "i2cBegin: using the board's own bus (panel, sensors)");
    }
    return moth_null();
  }
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

static moth_value n_i2c_ping(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (!s_i2c_bus || argv[0].type != MV_INT) return moth_bool(false);
  return moth_bool(i2c_master_probe(s_i2c_bus, (uint16_t)argv[0].as.i, 50) == ESP_OK);
}

static moth_value n_i2c_write_reg(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT || argv[2].type != MV_INT) {
    return moth_bool(false);
  }
  i2c_master_dev_handle_t dev = i2c_device((uint8_t)argv[0].as.i);
  if (!dev) return moth_bool(false);
  uint8_t buf[2] = {(uint8_t)argv[1].as.i, (uint8_t)argv[2].as.i};
  return moth_bool(i2c_master_transmit(dev, buf, 2, 100) == ESP_OK);
}

static moth_value n_i2c_read_reg(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_int(-1);
  i2c_master_dev_handle_t dev = i2c_device((uint8_t)argv[0].as.i);
  if (!dev) return moth_int(-1);
  uint8_t reg = (uint8_t)argv[1].as.i, out = 0;
  if (i2c_master_transmit_receive(dev, &reg, 1, &out, 1, 100) != ESP_OK) return moth_int(-1);
  return moth_int(out);
}

static moth_value n_i2c_read_bytes(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  moth_value list = moth_new_list(vm);
  if (argv[0].type != MV_INT || argv[1].type != MV_INT || argv[2].type != MV_INT) {
    return list;
  }
  int64_t n = argv[2].as.i;
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  i2c_master_dev_handle_t dev = i2c_device((uint8_t)argv[0].as.i);
  if (!dev) return list; /* empty list means "no answer" */
  uint8_t reg = (uint8_t)argv[1].as.i;
  uint8_t buf[64];
  if (i2c_master_transmit_receive(dev, &reg, 1, buf, (size_t)n, 100) != ESP_OK) {
    return list;
  }
  for (int64_t i = 0; i < n; i++) {
    moth_list_append(vm, list, moth_int(buf[i]));
  }
  return list;
}

static moth_value n_i2c_write_bytes(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT || !moth_is_list(argv[2])) {
    return moth_bool(false);
  }
  int count = moth_list_length(argv[2]);
  if (count > 64) count = 64;
  i2c_master_dev_handle_t dev = i2c_device((uint8_t)argv[0].as.i);
  if (!dev) return moth_bool(false);
  uint8_t buf[65];
  buf[0] = (uint8_t)argv[1].as.i;
  for (int i = 0; i < count; i++) {
    moth_value item = moth_list_at(argv[2], i);
    buf[1 + i] = item.type == MV_INT ? (uint8_t)item.as.i : 0;
  }
  return moth_bool(i2c_master_transmit(dev, buf, (size_t)(1 + count), 100) == ESP_OK);
}

/* ---- prefs — NVS-backed, so a value survives reboots ------------------- */

/* NVS limits keys to 15 chars; the Dart layer documents it. */
static bool prefs_key(const moth_value v, char out[16]) {
  int len = 0;
  const char *chars = moth_string_chars(v, &len);
  if (!chars || len == 0 || len > 15) return false;
  memcpy(out, chars, (size_t)len);
  out[len] = '\0';
  return true;
}

static moth_value n_prefs_get_int(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  char key[16];
  if (!prefs_key(argv[0], key)) return argv[1];
  nvs_handle_t h;
  if (nvs_open("prefs", NVS_READONLY, &h) != ESP_OK) return argv[1];
  int64_t value = 0;
  esp_err_t err = nvs_get_i64(h, key, &value);
  nvs_close(h);
  return err == ESP_OK ? moth_int(value) : argv[1];
}

static moth_value n_prefs_set_int(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  char key[16];
  if (!prefs_key(argv[0], key) || argv[1].type != MV_INT) return moth_bool(false);
  nvs_handle_t h;
  if (nvs_open("prefs", NVS_READWRITE, &h) != ESP_OK) return moth_bool(false);
  bool ok = nvs_set_i64(h, key, argv[1].as.i) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return moth_bool(ok);
}

/* ---- UART -------------------------------------------------------------- */

static moth_value n_uart_begin(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
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

static moth_value n_uart_write(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_INT) return moth_null();
  uint8_t byte = (uint8_t)argv[1].as.i;
  uart_write_bytes((uart_port_t)argv[0].as.i, &byte, 1);
  return moth_null();
}

static moth_value n_uart_available(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_int(0);
  size_t n = 0;
  uart_get_buffered_data_len((uart_port_t)argv[0].as.i, &n);
  return moth_int((int64_t)n);
}

static moth_value n_uart_read(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (argv[0].type != MV_INT) return moth_int(-1);
  uint8_t byte = 0;
  int n = uart_read_bytes((uart_port_t)argv[0].as.i, &byte, 1, 0);
  return moth_int(n == 1 ? byte : -1);
}

/* ---- registration ------------------------------------------------------- */

void moth_hw_register(moth_vm *vm) {
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
  moth_register(vm, "i2cReadBytes", n_i2c_read_bytes, NULL);
  moth_register(vm, "i2cWriteBytes", n_i2c_write_bytes, NULL);
  moth_register(vm, "prefsGetInt", n_prefs_get_int, NULL);
  moth_register(vm, "prefsSetInt", n_prefs_set_int, NULL);
  moth_register(vm, "servoAttach", n_servo_attach, NULL);
  moth_register(vm, "servoMicroseconds", n_servo_us, NULL);
  moth_register(vm, "uartBegin", n_uart_begin, NULL);
  moth_register(vm, "uartWrite", n_uart_write, NULL);
  moth_register(vm, "uartAvailable", n_uart_available, NULL);
  moth_register(vm, "uartRead", n_uart_read, NULL);
}
