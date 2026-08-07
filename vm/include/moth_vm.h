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

#define MOTH_BYTECODE_VERSION 1

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
} moth_status;

typedef enum { MV_NULL, MV_BOOL, MV_INT, MV_DOUBLE } moth_type;

typedef struct {
  moth_type type;
  union {
    bool b;
    int64_t i;
    double d;
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

/* A native receives its arguments left-to-right. Returning moth_null() is
 * the convention for void. Natives must not re-enter the VM. */
typedef moth_value (*moth_native_fn)(int argc, const moth_value *argv, void *user);

moth_vm *moth_new(void);
void moth_free(moth_vm *vm);

/* Register before loading: moth_load resolves the blob's native table
 * against these and fails if any name is missing. */
void moth_register(moth_vm *vm, const char *name, moth_native_fn fn, void *user);

/* Blob must outlive the VM — it is referenced, not copied. */
moth_status moth_load(moth_vm *vm, const uint8_t *blob, size_t len);

/* Runs the entry function to completion. */
moth_status moth_run(moth_vm *vm);

/* Human-readable description of the last failure ("" when none). Includes
 * the function name and pc for runtime traps. */
const char *moth_error(const moth_vm *vm);

#ifdef __cplusplus
}
#endif
#endif /* MOTH_VM_H */
