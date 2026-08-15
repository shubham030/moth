/* mothrun — runs a .mothb on the desktop with simulated peripherals.
 *
 * Pin writes, I2C and UART traffic, and delays are printed as a trace against
 * a virtual clock, so a program is visible (and testable) with no hardware
 * and no waiting. --real-time actually sleeps instead.
 */
#include "moth_vm.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_PINS 64
#define MAX_I2C_DEVICES 16
#define UART_PORTS 3

typedef struct {
  int64_t clock_ms;
  int64_t stop_after_ms; /* <0 = run forever */
  bool real_time;
  bool trace;

  bool level[MAX_PINS];
  bool is_output[MAX_PINS];
  bool configured[MAX_PINS];
  int analog_in[MAX_PINS];

  uint8_t i2c_devices[MAX_I2C_DEVICES];
  int n_i2c_devices;
  bool i2c_started;
  uint8_t i2c_regs[MAX_I2C_DEVICES][256];

  bool uart_open[UART_PORTS];

  uint64_t rng; /* deterministic by default so tests are stable */
} sim;

static sim g_sim;

static void trace(const char *fmt, ...) {
  if (!g_sim.trace) return;
  printf("[%6" PRId64 "ms] ", g_sim.clock_ms);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
}

static void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "moth: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

static int64_t want_int(moth_value v, const char *what) {
  if (v.type != MV_INT) die("%s must be a whole number", what);
  return v.as.i;
}

static int check_pin(moth_value v) {
  int64_t pin = want_int(v, "a pin number");
  if (pin < 0 || pin >= MAX_PINS) die("pin %" PRId64 " is out of range (0..%d)", pin, MAX_PINS - 1);
  return (int)pin;
}

static void advance_clock(int64_t ms) {
  g_sim.clock_ms += ms;
  if (g_sim.stop_after_ms >= 0 && g_sim.clock_ms >= g_sim.stop_after_ms) {
    printf("-- stopped after %" PRId64 "ms (simulated) --\n", g_sim.clock_ms);
    exit(0);
  }
}

/* ---- output ----------------------------------------------------------- */

static moth_value n_print(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  moth_value text = moth_to_string(vm, argv[0]);
  int len = 0;
  const char *chars = moth_string_chars(text, &len);
  if (chars) fwrite(chars, 1, (size_t)len, stdout);
  putchar('\n');
  fflush(stdout);
  return moth_null();
}

/* ---- timing ----------------------------------------------------------- */

static moth_value n_delay(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t ms = want_int(argv[0], "delay()");
  if (ms < 0) die("delay() needs a non-negative number of milliseconds");
  if (g_sim.real_time) usleep((useconds_t)(ms * 1000));
  advance_clock(ms);
  return moth_null();
}

static moth_value n_delay_us(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t us = want_int(argv[0], "delayMicroseconds()");
  if (us < 0) die("delayMicroseconds() needs a non-negative number");
  if (g_sim.real_time) usleep((useconds_t)us);
  advance_clock(us / 1000);
  return moth_null();
}

/* Under --real-time the clock is the wall clock, so timing a program (a
 * benchmark, say) measures something. Otherwise it is the virtual clock,
 * which only advances on delay() and keeps traces deterministic. */
static int64_t real_micros(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int64_t g_start_us;

static moth_value n_millis(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  if (g_sim.real_time) return moth_int((real_micros() - g_start_us) / 1000);
  return moth_int(g_sim.clock_ms);
}
static moth_value n_micros(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)v; (void)u;
  if (g_sim.real_time) return moth_int(real_micros() - g_start_us);
  return moth_int(g_sim.clock_ms * 1000);
}

/* ---- digital I/O ------------------------------------------------------ */

static moth_value pin_mode(const moth_value *argv, bool output, bool pullup) {
  int pin = check_pin(argv[0]);
  g_sim.is_output[pin] = output;
  g_sim.configured[pin] = true;
  g_sim.level[pin] = pullup; /* floating inputs read low, pulled-up read high */
  trace("pin %d -> %s", pin, output ? "output" : (pullup ? "input (pull-up)" : "input"));
  return moth_null();
}

static moth_value n_pin_output(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)u; return pin_mode(v, true, false);
}
static moth_value n_pin_input(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)u; return pin_mode(v, false, false);
}
static moth_value n_pin_input_pullup(moth_vm *vm, int c, const moth_value *v, void *u) {
  (void)vm; (void)c; (void)u; return pin_mode(v, false, true);
}

static moth_value n_digital_write(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int pin = check_pin(argv[0]);
  if (argv[1].type != MV_BOOL) die("digitalWrite(pin, value) needs true or false");
  if (!g_sim.configured[pin] || !g_sim.is_output[pin]) {
    die("pin %d was written before pinOutput(%d) — configure it first", pin, pin);
  }
  g_sim.level[pin] = argv[1].as.b;
  trace("pin %d = %s", pin, argv[1].as.b ? "HIGH" : "low");
  return moth_null();
}

static moth_value n_digital_read(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  return moth_bool(g_sim.level[check_pin(argv[0])]);
}

/* ---- analog I/O ------------------------------------------------------- */

static moth_value n_analog_read(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int pin = check_pin(argv[0]);
  trace("analogRead(%d) -> %d", pin, g_sim.analog_in[pin]);
  return moth_int(g_sim.analog_in[pin]);
}

static moth_value n_analog_write(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int pin = check_pin(argv[0]);
  int64_t duty = want_int(argv[1], "analogWrite() duty");
  if (duty < 0 || duty > 255) die("analogWrite() duty must be 0..255");
  trace("pin %d PWM duty %" PRId64 "/255", pin, duty);
  return moth_null();
}

static moth_value n_tone(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int pin = check_pin(argv[0]);
  trace("pin %d tone %" PRId64 " Hz", pin, want_int(argv[1], "tone() frequency"));
  return moth_null();
}

static moth_value n_no_tone(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  trace("pin %d tone off", check_pin(argv[0]));
  return moth_null();
}

/* ---- random ----------------------------------------------------------- */

static moth_value n_random_seed(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  g_sim.rng = (uint64_t)want_int(argv[0], "randomSeed()") | 1;
  return moth_null();
}

static moth_value n_random(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int64_t max = want_int(argv[0], "random()");
  if (max <= 0) die("random(max) needs max > 0");
  /* xorshift64: deterministic across runs so golden tests stay stable */
  uint64_t x = g_sim.rng;
  x ^= x << 13; x ^= x >> 7; x ^= x << 17;
  g_sim.rng = x;
  return moth_int((int64_t)(x % (uint64_t)max));
}

/* ---- I2C -------------------------------------------------------------- */

static int i2c_slot(uint8_t addr) {
  for (int i = 0; i < g_sim.n_i2c_devices; i++) {
    if (g_sim.i2c_devices[i] == addr) return i;
  }
  return -1;
}

static moth_value n_i2c_begin(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int sda = check_pin(argv[0]), scl = check_pin(argv[1]);
  g_sim.i2c_started = true;
  trace("i2c started on sda %d, scl %d", sda, scl);
  return moth_null();
}

static moth_value n_i2c_ping(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (!g_sim.i2c_started) die("call i2cBegin(sda, scl) before using I2C");
  int64_t addr = want_int(argv[0], "an I2C address");
  bool found = i2c_slot((uint8_t)addr) >= 0;
  if (found) trace("i2c device found at 0x%02" PRIx64, addr);
  return moth_bool(found);
}

static moth_value n_i2c_write_reg(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (!g_sim.i2c_started) die("call i2cBegin(sda, scl) before using I2C");
  int64_t addr = want_int(argv[0], "an I2C address");
  int64_t reg = want_int(argv[1], "a register number");
  int64_t val = want_int(argv[2], "a register value");
  int slot = i2c_slot((uint8_t)addr);
  if (slot < 0) return moth_bool(false);
  g_sim.i2c_regs[slot][reg & 0xFF] = (uint8_t)val;
  trace("i2c 0x%02" PRIx64 " reg 0x%02" PRIx64 " <- %" PRId64, addr, reg, val);
  return moth_bool(true);
}

static moth_value n_i2c_read_reg(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  if (!g_sim.i2c_started) die("call i2cBegin(sda, scl) before using I2C");
  int64_t addr = want_int(argv[0], "an I2C address");
  int64_t reg = want_int(argv[1], "a register number");
  int slot = i2c_slot((uint8_t)addr);
  if (slot < 0) return moth_int(-1); /* -1 means "no answer" */
  int v = g_sim.i2c_regs[slot][reg & 0xFF];
  trace("i2c 0x%02" PRIx64 " reg 0x%02" PRIx64 " -> %d", addr, reg, v);
  return moth_int(v);
}

static moth_value n_i2c_read_bytes(moth_vm *vm, int argc, const moth_value *argv,
                                   void *user) {
  (void)argc; (void)user;
  if (!g_sim.i2c_started) die("call i2cBegin(sda, scl) before using I2C");
  int64_t addr = want_int(argv[0], "an I2C address");
  int64_t reg = want_int(argv[1], "a register number");
  int64_t n = want_int(argv[2], "a byte count");
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  moth_value list = moth_new_list(vm);
  int slot = i2c_slot((uint8_t)addr);
  if (slot < 0) return list; /* empty list means "no answer" */
  for (int64_t i = 0; i < n; i++) {
    moth_list_append(vm, list, moth_int(g_sim.i2c_regs[slot][(reg + i) & 0xFF]));
  }
  trace("i2c 0x%02" PRIx64 " read %" PRId64 " bytes from reg 0x%02" PRIx64, addr,
        n, reg);
  return list;
}

static moth_value n_i2c_write_bytes(moth_vm *vm, int argc, const moth_value *argv,
                                    void *user) {
  (void)vm; (void)argc; (void)user;
  if (!g_sim.i2c_started) die("call i2cBegin(sda, scl) before using I2C");
  int64_t addr = want_int(argv[0], "an I2C address");
  int64_t reg = want_int(argv[1], "a register number");
  if (!moth_is_list(argv[2])) die("i2cWriteBytes wants a List<int> of bytes");
  int slot = i2c_slot((uint8_t)addr);
  if (slot < 0) return moth_bool(false);
  int count = moth_list_length(argv[2]);
  for (int i = 0; i < count; i++) {
    moth_value item = moth_list_at(argv[2], i);
    g_sim.i2c_regs[slot][(reg + i) & 0xFF] =
        (uint8_t)want_int(item, "a byte in the list");
  }
  trace("i2c 0x%02" PRIx64 " wrote %d bytes at reg 0x%02" PRIx64, addr, count, reg);
  return moth_bool(true);
}

/* ---- prefs — in-memory here; NVS on a board -------------------------- */

#define MAX_PREFS 64
static struct { char key[16]; int64_t value; bool used; } g_prefs[MAX_PREFS];

static int prefs_find(const char *key, size_t len) {
  if (len == 0 || len > 15) return -2; /* NVS's own key limit */
  for (int i = 0; i < MAX_PREFS; i++) {
    if (g_prefs[i].used && strncmp(g_prefs[i].key, key, len) == 0 &&
        g_prefs[i].key[len] == '\0') {
      return i;
    }
  }
  return -1;
}

static moth_value n_prefs_get_int(moth_vm *vm, int argc, const moth_value *argv,
                                  void *user) {
  (void)vm; (void)argc; (void)user;
  int len = 0;
  const char *key = moth_string_chars(argv[0], &len);
  if (!key) die("prefsGetInt wants a string key");
  int slot = prefs_find(key, (size_t)len);
  if (slot < 0) return argv[1]; /* absent or invalid key -> the fallback */
  trace("prefs '%.*s' -> %" PRId64, len, key, g_prefs[slot].value);
  return moth_int(g_prefs[slot].value);
}

static moth_value n_prefs_set_int(moth_vm *vm, int argc, const moth_value *argv,
                                  void *user) {
  (void)vm; (void)argc; (void)user;
  int len = 0;
  const char *key = moth_string_chars(argv[0], &len);
  if (!key) die("prefsSetInt wants a string key");
  if (len == 0 || len > 15) return moth_bool(false);
  int64_t value = want_int(argv[1], "an int value");
  int slot = prefs_find(key, (size_t)len);
  if (slot == -1) {
    for (int i = 0; i < MAX_PREFS; i++) {
      if (!g_prefs[i].used) { slot = i; break; }
    }
    if (slot < 0) return moth_bool(false);
    memcpy(g_prefs[slot].key, key, (size_t)len);
    g_prefs[slot].key[len] = '\0';
    g_prefs[slot].used = true;
  }
  g_prefs[slot].value = value;
  trace("prefs '%.*s' = %" PRId64, len, key, value);
  return moth_bool(true);
}

/* ---- servo — a trace here; 50Hz LEDC on a board ----------------------- */

static moth_value n_servo_attach(moth_vm *vm, int argc, const moth_value *argv,
                                 void *user) {
  (void)vm; (void)argc; (void)user;
  trace("pin %d servo attached (50Hz)", check_pin(argv[0]));
  return moth_null();
}

static moth_value n_servo_us(moth_vm *vm, int argc, const moth_value *argv,
                             void *user) {
  (void)vm; (void)argc; (void)user;
  int pin = check_pin(argv[0]);
  int64_t us = want_int(argv[1], "a pulse width in microseconds");
  if (us < 500) us = 500;
  if (us > 2500) us = 2500;
  trace("pin %d servo %" PRId64 "us", pin, us);
  return moth_null();
}

/* ---- UART ------------------------------------------------------------- */

static int check_uart(moth_value v) {
  int64_t port = want_int(v, "a UART port");
  if (port < 0 || port >= UART_PORTS) die("UART port must be 0..%d", UART_PORTS - 1);
  return (int)port;
}

static moth_value n_uart_begin(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int port = check_uart(argv[0]);
  int tx = check_pin(argv[1]), rx = check_pin(argv[2]);
  int64_t baud = want_int(argv[3], "a baud rate");
  g_sim.uart_open[port] = true;
  trace("uart %d open: tx %d, rx %d, %" PRId64 " baud", port, tx, rx, baud);
  return moth_null();
}

static moth_value n_uart_write(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  int port = check_uart(argv[0]);
  if (!g_sim.uart_open[port]) die("call uartBegin() before writing to UART %d", port);
  trace("uart %d <- %" PRId64, port, want_int(argv[1], "a byte"));
  return moth_null();
}

static moth_value n_uart_available(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  check_uart(argv[0]);
  return moth_int(0); /* nothing is wired to the simulator's UART */
}

static moth_value n_uart_read(moth_vm *vm, int argc, const moth_value *argv, void *user) {
  (void)vm; (void)argc; (void)user;
  check_uart(argv[0]);
  return moth_int(-1);
}

/* ---- driver ----------------------------------------------------------- */

static const char *status_text(moth_status st) {
  switch (st) {
    case MOTH_OK: return "ok";
    case MOTH_ERR_FORMAT: return "bad program file";
    case MOTH_ERR_UNRESOLVED_NATIVE: return "missing built-in";
    case MOTH_ERR_TYPE: return "type error";
    case MOTH_ERR_DIV_ZERO: return "division by zero";
    case MOTH_ERR_STACK_OVERFLOW: return "stack overflow";
    case MOTH_ERR_OOM: return "out of memory";
    case MOTH_ERR_BAD_OP: return "corrupt program";
    case MOTH_HALTED: return "stopped";
  }
  return "error";
}

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

static void usage(void) {
  fprintf(stderr,
          "usage: mothrun <program.mothb> [options]\n"
          "  --real-time        actually sleep on delay() instead of simulating\n"
          "  --quiet            suppress the pin/bus trace\n"
          "  --stop-after MS    halt once the simulated clock reaches MS\n"
          "  --analog PIN=VAL   value analogRead(PIN) should return\n"
          "  --i2c-device ADDR  pretend a device answers at ADDR (e.g. 0x5a)\n"
          "  --i2c-reg A:R=V    preload a device register (e.g. 0x48:0=25)\n"
          "  --seed N           seed the random() generator (default 1)\n");
}

int main(int argc, char **argv) {
  const char *path = NULL;
  g_sim.stop_after_ms = -1;
  g_sim.trace = true;
  g_sim.rng = 1;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (strcmp(a, "--real-time") == 0) g_sim.real_time = true;
    else if (strcmp(a, "--quiet") == 0) g_sim.trace = false;
    else if (strcmp(a, "--stop-after") == 0 && i + 1 < argc)
      g_sim.stop_after_ms = strtoll(argv[++i], NULL, 10);
    else if (strcmp(a, "--seed") == 0 && i + 1 < argc)
      g_sim.rng = (uint64_t)strtoll(argv[++i], NULL, 10) | 1;
    else if (strcmp(a, "--analog") == 0 && i + 1 < argc) {
      int pin, val;
      if (sscanf(argv[++i], "%d=%d", &pin, &val) != 2 || pin < 0 || pin >= MAX_PINS) {
        die("--analog wants PIN=VALUE");
      }
      g_sim.analog_in[pin] = val;
    } else if (strcmp(a, "--i2c-device") == 0 && i + 1 < argc) {
      if (g_sim.n_i2c_devices >= MAX_I2C_DEVICES) die("too many --i2c-device entries");
      g_sim.i2c_devices[g_sim.n_i2c_devices++] = (uint8_t)strtol(argv[++i], NULL, 0);
    } else if (strcmp(a, "--i2c-reg") == 0 && i + 1 < argc) {
      /* ADDR:REG=VAL — pretend a sensor already holds this register value */
      int addr = 0, reg = 0, val = 0;
      if (sscanf(argv[++i], "%i:%i=%i", &addr, &reg, &val) != 3) {
        die("--i2c-reg wants ADDR:REG=VALUE, e.g. 0x48:0=25");
      }
      int slot = i2c_slot((uint8_t)addr);
      if (slot < 0) {
        if (g_sim.n_i2c_devices >= MAX_I2C_DEVICES) die("too many I2C devices");
        slot = g_sim.n_i2c_devices++;
        g_sim.i2c_devices[slot] = (uint8_t)addr;
      }
      g_sim.i2c_regs[slot][reg & 0xFF] = (uint8_t)val;
    } else if (a[0] == '-') { usage(); return 64; }
    else if (!path) path = a;
    else { usage(); return 64; }
  }
  if (!path) { usage(); return 64; }

  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "mothrun: cannot open %s\n", path); return 66; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *blob = malloc((size_t)len);
  if (!blob || fread(blob, 1, (size_t)len, f) != (size_t)len) {
    fprintf(stderr, "mothrun: cannot read %s\n", path);
    return 66;
  }
  fclose(f);

  g_start_us = real_micros();
  moth_vm *vm = moth_new();
  register_all(vm);

  moth_status st = moth_load(vm, blob, (size_t)len);
  if (st != MOTH_OK) {
    fprintf(stderr, "mothrun: %s: %s\n", status_text(st), moth_error(vm));
    return 65;
  }
  st = moth_run(vm);
  if (st != MOTH_OK) {
    fprintf(stderr, "mothrun: %s: %s\n", status_text(st), moth_error(vm));
    return 70;
  }
  moth_free(vm);
  free(blob);
  return 0;
}
