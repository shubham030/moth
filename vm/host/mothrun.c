/* mothrun — runs a .mothb on the desktop with simulated peripherals.
 *
 * Pin writes and delays are printed as a trace against a virtual clock, so a
 * blink program is visible (and testable) with no hardware and no waiting.
 * --real-time actually sleeps instead.
 */
#include "moth_vm.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PINS 64

typedef struct {
  int64_t clock_ms;
  int64_t stop_after_ms; /* <0 = run forever */
  bool real_time;
  bool trace;
  bool level[MAX_PINS];
  bool is_output[MAX_PINS];
  bool configured[MAX_PINS];
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

static bool check_pin(int64_t pin) {
  if (pin < 0 || pin >= MAX_PINS) {
    fprintf(stderr, "moth: pin %" PRId64 " is out of range (0..%d)\n", pin, MAX_PINS - 1);
    exit(1);
  }
  return true;
}

static void print_value(moth_value v) {
  switch (v.type) {
    case MV_NULL: printf("null"); break;
    case MV_BOOL: printf(v.as.b ? "true" : "false"); break;
    case MV_INT: printf("%" PRId64, v.as.i); break;
    case MV_DOUBLE: {
      char buf[32];
      snprintf(buf, sizeof buf, "%g", v.as.d);
      printf("%s", buf);
      if (!strpbrk(buf, ".einf")) printf(".0"); /* Dart prints 1.0, not 1 */
      break;
    }
  }
}

static moth_value n_print(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  print_value(argv[0]);
  putchar('\n');
  fflush(stdout);
  return moth_null();
}

static moth_value n_delay(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[0].as.i < 0) {
    fprintf(stderr, "moth: delay() needs a non-negative whole number of milliseconds\n");
    exit(1);
  }
  int64_t ms = argv[0].as.i;
  if (g_sim.real_time) usleep((useconds_t)(ms * 1000));
  g_sim.clock_ms += ms;
  if (g_sim.stop_after_ms >= 0 && g_sim.clock_ms >= g_sim.stop_after_ms) {
    printf("-- stopped after %" PRId64 "ms (simulated) --\n", g_sim.clock_ms);
    exit(0);
  }
  return moth_null();
}

static moth_value n_millis(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)argv; (void)user;
  return moth_int(g_sim.clock_ms);
}

static moth_value pin_mode(const moth_value *argv, bool output, bool pullup) {
  int64_t pin = argv[0].as.i;
  if (argv[0].type != MV_INT) {
    fprintf(stderr, "moth: pin number must be a whole number\n");
    exit(1);
  }
  check_pin(pin);
  g_sim.is_output[pin] = output;
  g_sim.configured[pin] = true;
  g_sim.level[pin] = pullup; /* floating inputs read low, pulled-up read high */
  trace("pin %" PRId64 " -> %s", pin,
        output ? "output" : (pullup ? "input (pull-up)" : "input"));
  return moth_null();
}

static moth_value n_pin_output(int c, const moth_value *v, void *u) {
  (void)c; (void)u; return pin_mode(v, true, false);
}
static moth_value n_pin_input(int c, const moth_value *v, void *u) {
  (void)c; (void)u; return pin_mode(v, false, false);
}
static moth_value n_pin_input_pullup(int c, const moth_value *v, void *u) {
  (void)c; (void)u; return pin_mode(v, false, true);
}

static moth_value n_digital_write(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT || argv[1].type != MV_BOOL) {
    fprintf(stderr, "moth: digitalWrite(pin, value) needs a pin number and true/false\n");
    exit(1);
  }
  int64_t pin = argv[0].as.i;
  check_pin(pin);
  if (!g_sim.configured[pin] || !g_sim.is_output[pin]) {
    fprintf(stderr,
            "moth: pin %" PRId64 " was written before pinOutput(%" PRId64 ") — "
            "configure it first\n", pin, pin);
    exit(1);
  }
  g_sim.level[pin] = argv[1].as.b;
  trace("pin %" PRId64 " = %s", pin, argv[1].as.b ? "HIGH" : "low");
  return moth_null();
}

static moth_value n_digital_read(int argc, const moth_value *argv, void *user) {
  (void)argc; (void)user;
  if (argv[0].type != MV_INT) {
    fprintf(stderr, "moth: digitalRead(pin) needs a pin number\n");
    exit(1);
  }
  int64_t pin = argv[0].as.i;
  check_pin(pin);
  return moth_bool(g_sim.level[pin]);
}

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
  }
  return "error";
}

static void usage(void) {
  fprintf(stderr,
          "usage: mothrun <program.mothb> [--real-time] [--quiet] [--stop-after MS]\n"
          "  --real-time     actually sleep on delay() instead of simulating\n"
          "  --quiet         suppress the pin/timing trace\n"
          "  --stop-after N  halt once the simulated clock reaches N ms\n");
}

int main(int argc, char **argv) {
  const char *path = NULL;
  g_sim.stop_after_ms = -1;
  g_sim.trace = true;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--real-time") == 0) g_sim.real_time = true;
    else if (strcmp(argv[i], "--quiet") == 0) g_sim.trace = false;
    else if (strcmp(argv[i], "--stop-after") == 0 && i + 1 < argc)
      g_sim.stop_after_ms = strtoll(argv[++i], NULL, 10);
    else if (argv[i][0] == '-') { usage(); return 64; }
    else if (!path) path = argv[i];
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

  moth_vm *vm = moth_new();
  moth_register(vm, "print", n_print, NULL);
  moth_register(vm, "delay", n_delay, NULL);
  moth_register(vm, "millis", n_millis, NULL);
  moth_register(vm, "pinOutput", n_pin_output, NULL);
  moth_register(vm, "pinInput", n_pin_input, NULL);
  moth_register(vm, "pinInputPullup", n_pin_input_pullup, NULL);
  moth_register(vm, "digitalWrite", n_digital_write, NULL);
  moth_register(vm, "digitalRead", n_digital_read, NULL);

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
