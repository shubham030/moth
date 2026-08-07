#ifndef MOTH_INTERNAL_H
#define MOTH_INTERNAL_H

#include "moth_vm.h"

/* Opcodes — docs/BYTECODE.md is normative. */
enum {
  OP_NOP = 0x00,
  OP_CONST = 0x01,
  OP_INT8 = 0x02,
  OP_TRUE = 0x03,
  OP_FALSE = 0x04,
  OP_NULL = 0x05,
  OP_POP = 0x06,
  OP_DUP = 0x07,
  OP_LOAD = 0x08,
  OP_STORE = 0x09,
  OP_LOAD_GLOBAL = 0x0A,
  OP_STORE_GLOBAL = 0x0B,

  OP_ADD = 0x10,
  OP_SUB = 0x11,
  OP_MUL = 0x12,
  OP_DIV = 0x13,
  OP_IDIV = 0x14,
  OP_MOD = 0x15,
  OP_NEG = 0x16,
  OP_BAND = 0x17,
  OP_BOR = 0x18,
  OP_BXOR = 0x19,
  OP_SHL = 0x1A,
  OP_SHR = 0x1B,
  OP_BNOT = 0x1C,
  OP_TO_STRING = 0x1D,

  OP_EQ = 0x20,
  OP_NE = 0x21,
  OP_LT = 0x22,
  OP_LE = 0x23,
  OP_GT = 0x24,
  OP_GE = 0x25,
  OP_NOT = 0x26,

  OP_JUMP = 0x30,
  OP_JUMP_IF_FALSE = 0x31,
  OP_JUMP_IF_FALSE_K = 0x32,
  OP_JUMP_IF_TRUE_K = 0x33,

  OP_CALL = 0x40,
  OP_NATIVE = 0x41,
  OP_RET = 0x42,
  OP_RET_NULL = 0x43,
};

enum { CONST_INT = 0, CONST_DOUBLE = 1, CONST_STRING = 2, CONST_BOOL = 3, CONST_NULL = 4 };

#define MOTH_NO_INIT 0xFFFF

typedef struct {
  const char *chars; /* into the blob; not NUL-terminated */
  uint16_t len;
} moth_str;

/* ---- heap objects ------------------------------------------------------ */

typedef enum { OBJ_STRING } obj_type;

struct moth_obj {
  obj_type type;
  bool marked;
  struct moth_obj *next; /* every object, for the sweep walk */
};

typedef struct {
  moth_obj obj;
  uint32_t len;
  /* Constant strings point straight into the blob and are not freed; only
   * strings built at runtime own their bytes. */
  bool owns_chars;
  const char *chars;
} moth_string;

#define AS_STRING(v) ((moth_string *)(v).as.obj)
#define IS_OBJ_TYPE(v, t) ((v).type == MV_OBJ && (v).as.obj->type == (t))

/* object.c */
moth_value moth_string_take(moth_vm *vm, char *chars, uint32_t len);
moth_value moth_string_borrow(moth_vm *vm, const char *chars, uint32_t len);
moth_value moth_concat(moth_vm *vm, moth_value a, moth_value b);
moth_value moth_to_string(moth_vm *vm, moth_value v);
bool moth_string_equal(moth_value a, moth_value b);
void moth_collect(moth_vm *vm);
void moth_free_objects(moth_vm *vm);

typedef struct {
  uint16_t name_const;
  uint8_t arity;
  uint8_t nlocals;
  uint32_t code_len;
  const uint8_t *code;
} moth_func;

typedef struct {
  const char *name; /* owned NUL-terminated copy */
  moth_native_fn fn;
  void *user;
} moth_native_reg;

typedef struct {
  moth_native_fn fn; /* resolved at load */
  void *user;
  uint8_t argc;
} moth_native_slot;

typedef struct {
  const moth_func *fn;
  const uint8_t *ip;
  moth_value *slots; /* into vm->stack */
} moth_frame;

struct moth_vm {
  /* registrations (host-provided, before load) */
  moth_native_reg *regs;
  int nregs, cap_regs;

  /* loaded blob */
  const uint8_t *blob;
  size_t blob_len;
  moth_value *consts;   /* numeric/bool/null constants, MV_NULL for strings */
  moth_str *const_strs; /* string constants (len 0 when not a string) */
  uint16_t nconsts;
  moth_func *funcs;
  uint16_t nfuncs;
  moth_native_slot *natives;
  uint16_t nnatives;
  moth_value *globals;
  uint16_t nglobals;
  uint16_t entry;
  uint16_t init; /* MOTH_NO_INIT when the program has no top-level initializers */
  bool loaded;

  moth_value stack[MOTH_STACK_MAX];
  moth_value *sp;
  moth_frame frames[MOTH_FRAMES_MAX];
  int nframes;

  /* heap */
  moth_obj *objects;
  size_t bytes_allocated;
  size_t next_gc;
  bool gc_enabled; /* off while the constant table is being built */

  char err[192];
};

#endif /* MOTH_INTERNAL_H */
