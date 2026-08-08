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

  OP_NEW_LIST = 0x50, /* u16 count: pops that many values into a new list */
  OP_INDEX_GET = 0x51,
  OP_INDEX_SET = 0x52,

  OP_NEW_INSTANCE = 0x57, /* u16 class index */
  OP_GET_PROP = 0x58,     /* u16 name const: instance field, or .length */
  OP_SET_PROP = 0x59,     /* u16 name const */
  OP_INVOKE = 0x5A,       /* u16 name const, u8 argc */
  OP_CLOSURE = 0x5B,      /* u16 func index, u8 captures_this */
  OP_CALL_VALUE = 0x5C,   /* u8 argc: callee is beneath the arguments */

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

typedef enum { OBJ_STRING, OBJ_LIST, OBJ_INSTANCE, OBJ_CLOSURE } obj_type;

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

typedef struct {
  moth_obj obj;
  int count;
  int capacity;
  moth_value *items; /* plain malloc, not GC-tracked; contents are traced */
} moth_list;

typedef struct {
  moth_obj obj;
  uint16_t class_index;
  moth_value *fields; /* exactly the class's field count */
} moth_instance;

/* A closure is a function plus the receiver it was created with. moth does
 * not capture locals yet (the compiler rejects that), so there is no upvalue
 * array — `this` is the only thing a closure can close over. */
typedef struct {
  moth_obj obj;
  uint16_t func_index;
  moth_value receiver; /* null for a closure made outside a method */
} moth_closure;

#define AS_STRING(v) ((moth_string *)(v).as.obj)
#define AS_LIST(v) ((moth_list *)(v).as.obj)
#define AS_INSTANCE(v) ((moth_instance *)(v).as.obj)
#define AS_CLOSURE(v) ((moth_closure *)(v).as.obj)
#define IS_OBJ_TYPE(v, t) ((v).type == MV_OBJ && (v).as.obj->type == (t))
#define IS_LIST(v) IS_OBJ_TYPE(v, OBJ_LIST)
#define IS_INSTANCE(v) IS_OBJ_TYPE(v, OBJ_INSTANCE)
#define IS_CLOSURE(v) IS_OBJ_TYPE(v, OBJ_CLOSURE)

/* object.c */
moth_value moth_string_take(moth_vm *vm, char *chars, uint32_t len);
moth_value moth_string_borrow(moth_vm *vm, const char *chars, uint32_t len);
moth_value moth_concat(moth_vm *vm, moth_value a, moth_value b);
moth_value moth_list_new(moth_vm *vm);
bool moth_list_push(moth_vm *vm, moth_value list, moth_value item);
moth_value moth_instance_new(moth_vm *vm, uint16_t class_index, uint8_t nfields);
moth_value moth_closure_new(moth_vm *vm, uint16_t func_index, moth_value receiver);
bool moth_string_equal(moth_value a, moth_value b);
int moth_format_double(char *buf, size_t n, double d);
void moth_collect(moth_vm *vm);
void moth_free_objects(moth_vm *vm);

/* verify.c — abstract-interprets every function's stack depth at load. */
bool moth_verify(moth_vm *vm, char *err, size_t err_len);

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

/* Field and method names are compared by constant index: the pool dedupes,
 * so identical names always share one index. Lookup is a short scan. */
typedef struct {
  uint16_t name_const;
  uint16_t func_index;
} moth_method;

typedef struct {
  uint16_t name_const;
  uint8_t nfields;
  uint16_t *field_names;
  uint16_t nmethods;
  moth_method *methods;
  uint16_t ctor; /* function index, or MOTH_NO_CTOR */
} moth_class;

#define MOTH_NO_CTOR 0xFFFF

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
  moth_class *classes;
  uint16_t nclasses;
  /* constant indices of the built-in member names, resolved once at load
   * so property access is an integer compare rather than a string compare */
  uint16_t k_length, k_add, k_remove_last, k_clear;
  uint16_t entry;
  uint16_t init; /* MOTH_NO_INIT when the program has no top-level initializers */
  bool loaded;

  moth_value stack[MOTH_STACK_MAX];
  moth_value *sp;
  moth_frame frames[MOTH_FRAMES_MAX];
  int nframes;

  /* heap */
  moth_obj *objects;
  /* Marking uses an explicit worklist rather than recursion: one C frame per
   * nesting level would overflow a few-KB FreeRTOS task stack. */
  moth_obj **gray;
  int gray_count, gray_cap;
  bool gray_overflow;
  size_t bytes_allocated;
  size_t next_gc;
  bool gc_enabled; /* off while the constant table is being built */

  char err[192];
};

#endif /* MOTH_INTERNAL_H */
