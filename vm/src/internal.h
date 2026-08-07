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

  OP_ADD = 0x10,
  OP_SUB = 0x11,
  OP_MUL = 0x12,
  OP_DIV = 0x13,
  OP_IDIV = 0x14,
  OP_MOD = 0x15,
  OP_NEG = 0x16,

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

typedef struct {
  const char *chars; /* into the blob; not NUL-terminated */
  uint16_t len;
} moth_str;

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
  uint16_t entry;
  bool loaded;

  moth_value stack[MOTH_STACK_MAX];
  moth_value *sp;
  moth_frame frames[MOTH_FRAMES_MAX];
  int nframes;

  char err[192];
};

#endif /* MOTH_INTERNAL_H */
