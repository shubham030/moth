/* moth VM — loads a .mothb blob and interprets it.
 * Format and semantics are normative in docs/BYTECODE.md.
 *
 * Platform-free C11: no OS calls, no allocation beyond malloc/free. Hosts
 * (POSIX harness, ESP-IDF app) supply peripherals as registered natives.
 */
#ifndef MOTH_VM_H
#define MOTH_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump this whenever a blob stops being safe for an older VM to run: a new
 * opcode, a changed operand layout, a new section. Version 4 added OP_CLOSURE
 * and OP_CALL_VALUE, which a version-3 VM would reach at run time and trap on,
 * halfway through whatever the program had already done. Version 5 added a
 * kind byte to each class member, which changes the table's layout — an older
 * VM would misread every entry after the first. The check is exact, so an old
 * board refuses a new blob at load instead. */
#define MOTH_BYTECODE_VERSION 5

#ifndef MOTH_STACK_MAX
#define MOTH_STACK_MAX 256
#endif
#ifndef MOTH_FRAMES_MAX
#define MOTH_FRAMES_MAX 64
#endif

typedef enum {
  MOTH_OK = 0,
  MOTH_ERR_FORMAT,            /* malformed or wrong-version blob */
  MOTH_ERR_UNRESOLVED_NATIVE, /* blob needs a native the host didn't register */
  MOTH_ERR_TYPE,              /* operand of the wrong type */
  MOTH_ERR_DIV_ZERO,
  MOTH_ERR_STACK_OVERFLOW,
  MOTH_ERR_OOM,
  MOTH_ERR_BAD_OP,            /* unknown opcode / pc out of range */
  MOTH_HALTED,                /* stopped by moth_request_halt, not an error */
} moth_status;

typedef enum { MV_NULL, MV_BOOL, MV_INT, MV_DOUBLE, MV_OBJ } moth_type;

typedef struct moth_obj moth_obj;

typedef struct {
  moth_type type;
  union {
    bool b;
    int64_t i;
    double d;
    moth_obj *obj;
  } as;
} moth_value;

static inline moth_value moth_null(void) { moth_value v; v.type = MV_NULL; v.as.i = 0; return v; }
static inline moth_value moth_bool(bool b) { moth_value v; v.type = MV_BOOL; v.as.b = b; return v; }
static inline moth_value moth_int(int64_t i) { moth_value v; v.type = MV_INT; v.as.i = i; return v; }
static inline moth_value moth_double(double d) { moth_value v; v.type = MV_DOUBLE; v.as.d = d; return v; }

static inline bool moth_is_num(moth_value v) { return v.type == MV_INT || v.type == MV_DOUBLE; }
static inline double moth_as_double(moth_value v) {
  return v.type == MV_DOUBLE ? v.as.d : (double)v.as.i;
}

typedef struct moth_vm moth_vm;

/* ---- strings ----------------------------------------------------------
 * Heap-allocated and garbage collected. Chars are NOT NUL-terminated; use
 * the length. A string's memory is only valid until the next allocation
 * that triggers a collection, so copy it if you need to keep it. */
bool moth_is_string(moth_value v);
const char *moth_string_chars(moth_value v, int *len_out);

/* Allocates a string the collector owns. Safe to call from a native. */
moth_value moth_new_string(moth_vm *vm, const char *chars, int len);

/* Renders any value the way Dart's print() would — including lists and
 * shortest-round-trip doubles. Hosts should use this so print(x) and '$x'
 * can never disagree. */
moth_value moth_to_string(moth_vm *vm, moth_value v);

/* A native receives its arguments left-to-right. Returning moth_null() is
 * the convention for void. Natives must not re-enter the VM, but may
 * allocate through the vm handle. */
typedef moth_value (*moth_native_fn)(moth_vm *vm, int argc, const moth_value *argv,
                                     void *user);

moth_vm *moth_new(void);
void moth_free(moth_vm *vm);

/* Register before loading: moth_load resolves the blob's native table
 * against these and fails if any name is missing. */
void moth_register(moth_vm *vm, const char *name, moth_native_fn fn, void *user);

/* Blob must outlive the VM — it is referenced, not copied. */
moth_status moth_load(moth_vm *vm, const uint8_t *blob, size_t len);

/* Runs the entry function to completion. */
moth_status moth_run(moth_vm *vm);

/* Asks the running program to stop at the next instruction, returning
 * MOTH_HALTED from moth_run. Safe to call from a native — that is the point:
 * it is how a host swaps in a newly pushed program without waiting for a
 * loop that never ends. */
void moth_request_halt(moth_vm *vm);

/* Human-readable description of the last failure ("" when none). Includes
 * the function name and pc for runtime traps. */
const char *moth_error(const moth_vm *vm);

#ifdef __cplusplus
}
#endif
#endif /* MOTH_VM_H */
