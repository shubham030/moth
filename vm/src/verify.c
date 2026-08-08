/* Bytecode verification.
 *
 * The load-time table checks catch a truncated or mis-indexed blob, but they
 * say nothing about whether the code inside a function is coherent: a blob
 * could still jump into the middle of an instruction, or leave the operand
 * stack at a different depth on two paths that meet. The interpreter's own
 * guards would turn most of that into a clean trap, but "most" is not a
 * safety property, and M4 intends to accept blobs over the network.
 *
 * So before running anything, every function is abstractly interpreted: walk
 * all reachable paths tracking the operand-stack depth, and require that any
 * instruction reached twice is reached at the same depth. That rejects
 * unbalanced jumps, stack underflow and overflow, invalid operand indices,
 * and jumps that do not land on an instruction boundary.
 */
#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNVISITED (-1)

typedef struct {
  moth_vm *vm;
  const moth_func *fn;
  int *depth_at;  /* per byte offset; UNVISITED until reached */
  uint32_t *work; /* pcs still to walk */
  int work_count;
  char err[160];
} verifier;

static bool fail_at(verifier *v, uint32_t pc, const char *what) {
  snprintf(v->err, sizeof v->err, "%s at offset %" PRIu32, what, pc);
  return false;
}

/* Records the depth an instruction is reached at, and queues it the first
 * time. A second arrival with a different depth is the error this whole
 * pass exists to find. */
static bool reach(verifier *v, uint32_t pc, int depth) {
  if (pc >= v->fn->code_len) return fail_at(v, pc, "jump outside the function");
  if (depth < 0) return fail_at(v, pc, "stack underflow");
  if (depth > MOTH_STACK_MAX) return fail_at(v, pc, "stack overflow");

  if (v->depth_at[pc] == UNVISITED) {
    v->depth_at[pc] = depth;
    v->work[v->work_count++] = pc;
    return true;
  }
  if (v->depth_at[pc] != depth) {
    return fail_at(v, pc, "paths reach here with different stack depths");
  }
  return true;
}

static uint16_t read16(const uint8_t *code, uint32_t pc) {
  return (uint16_t)(code[pc] | (code[pc + 1] << 8));
}

/* One opcode's stack shape, as declared in MOTH_OPCODE_TABLE. */
typedef struct {
  uint8_t operands;
  uint8_t min_stack;
  int8_t net;
  bool known; /* false for the branch and call family, handled by hand */
} op_shape;

static op_shape shape_of(uint8_t op) {
  switch (op) {
#define MOTH_SHAPE_CASE(NAME, OPERANDS, MIN, NET) \
  case NAME:                                      \
    return (op_shape){(OPERANDS), (MIN), (NET), true};
    MOTH_OPCODE_TABLE(MOTH_SHAPE_CASE)
#undef MOTH_SHAPE_CASE
    default:
      return (op_shape){0, 0, 0, false};
  }
}

/* Walks one instruction, returning the pcs that follow it. */
static bool step(verifier *v, uint32_t pc, int depth) {
  const moth_func *fn = v->fn;
  const uint8_t *code = fn->code;
  moth_vm *vm = v->vm;
  uint8_t op = code[pc];
  uint32_t next = pc + 1;

  /* Operand bytes must exist before they are read. */
#define OPERANDS(n)                                                     \
  do {                                                                  \
    if (next + (n) > fn->code_len) return fail_at(v, pc, "truncated instruction"); \
    next += (n);                                                        \
  } while (0)

  /* Opcodes with a fixed shape come straight off the table, so the operand
   * count and the depth requirement cannot disagree with the interpreter's. */
  op_shape sh = shape_of(op);
  if (sh.known) {
    OPERANDS(sh.operands);
    if (depth < sh.min_stack) return fail_at(v, pc, "stack underflow");

    /* Operand indices are the one thing the table cannot express. */
    switch (op) {
      case OP_CONST:
        if (read16(code, pc + 1) >= vm->nconsts) return fail_at(v, pc, "constant out of range");
        break;
      case OP_GET_PROP: case OP_SET_PROP:
        if (read16(code, pc + 1) >= vm->nconsts)
          return fail_at(v, pc, "property name out of range");
        break;
      case OP_LOAD: case OP_STORE:
        if (code[pc + 1] >= fn->nlocals) return fail_at(v, pc, "local out of range");
        break;
      case OP_LOAD_GLOBAL: case OP_STORE_GLOBAL:
        if (read16(code, pc + 1) >= vm->nglobals) return fail_at(v, pc, "global out of range");
        break;
      case OP_NEW_INSTANCE:
        if (read16(code, pc + 1) >= vm->nclasses) return fail_at(v, pc, "unknown class");
        break;
      case OP_CLOSURE:
        if (read16(code, pc + 1) >= vm->nfuncs)
          return fail_at(v, pc, "closure over an unknown function");
        break;
      default:
        break;
    }

    depth += sh.net;
    if (next >= fn->code_len) {
      return fail_at(v, pc, "execution runs past the end of the function");
    }
    return reach(v, next, depth);
  }

  /* The rest either branch, or consume a count carried in their operands. */
  switch (op) {
    case OP_JUMP: {
      OPERANDS(2);
      int32_t target = (int32_t)next + (int16_t)read16(code, pc + 1);
      if (target < 0) return fail_at(v, pc, "jump outside the function");
      return reach(v, (uint32_t)target, depth); /* unconditional: no fallthrough */
    }
    case OP_JUMP_IF_FALSE: case OP_JUMP_IF_FALSE_K: case OP_JUMP_IF_TRUE_K: {
      OPERANDS(2);
      int32_t target = (int32_t)next + (int16_t)read16(code, pc + 1);
      if (target < 0) return fail_at(v, pc, "jump outside the function");
      /* All three read the value on top; only the non-K form pops it. */
      if (depth < 1) return fail_at(v, pc, "stack underflow");
      int taken = (op == OP_JUMP_IF_FALSE) ? depth - 1 : depth;
      if (op == OP_JUMP_IF_FALSE) depth -= 1;
      if (!reach(v, (uint32_t)target, taken)) return false;
      break;
    }

    case OP_CALL: {
      OPERANDS(3);
      uint16_t idx = read16(code, pc + 1);
      uint8_t argc = code[pc + 3];
      if (idx >= vm->nfuncs) return fail_at(v, pc, "call to an unknown function");
      if (vm->funcs[idx].arity != argc) return fail_at(v, pc, "wrong argument count");
      if (depth < argc) return fail_at(v, pc, "stack underflow");
      depth = depth - argc + 1;
      break;
    }
    case OP_NATIVE: {
      OPERANDS(3);
      uint16_t idx = read16(code, pc + 1);
      uint8_t argc = code[pc + 3];
      if (idx >= vm->nnatives) return fail_at(v, pc, "unknown built-in");
      if (vm->natives[idx].argc != argc) return fail_at(v, pc, "wrong built-in argument count");
      if (depth < argc) return fail_at(v, pc, "stack underflow");
      depth = depth - argc + 1;
      break;
    }
    case OP_INVOKE: {
      OPERANDS(3);
      if (read16(code, pc + 1) >= vm->nconsts) return fail_at(v, pc, "method name out of range");
      int argc = code[pc + 3];
      if (depth < argc + 1) return fail_at(v, pc, "stack underflow"); /* receiver too */
      depth = depth - argc; /* receiver and arguments out, one result in */
      break;
    }
    case OP_CALL_VALUE: {
      OPERANDS(1);
      int argc = code[pc + 1];
      if (depth < argc + 1) return fail_at(v, pc, "stack underflow"); /* callee too */
      depth = depth - argc; /* callee and arguments out, one result in */
      break;
    }

    case OP_RET:
      if (depth < 1) return fail_at(v, pc, "stack underflow");
      return true; /* path ends */
    case OP_RET_NULL:
      return true;

    case OP_NEW_LIST: {
      OPERANDS(2);
      uint16_t count = read16(code, pc + 1);
      if (depth < (int)count) return fail_at(v, pc, "stack underflow");
      depth = depth - (int)count + 1;
      break;
    }

    default:
      return fail_at(v, pc, "unknown opcode");
  }
#undef OPERANDS

  if (next >= fn->code_len) {
    return fail_at(v, pc, "execution runs past the end of the function");
  }
  return reach(v, next, depth);
}

static bool verify_function(verifier *v, const moth_func *fn) {
  v->fn = fn;
  v->work_count = 0;
  for (uint32_t i = 0; i < fn->code_len; i++) v->depth_at[i] = UNVISITED;

  /* Parameters occupy slots, not the operand stack, so entry depth is 0. */
  if (!reach(v, 0, 0)) return false;

  while (v->work_count > 0) {
    uint32_t pc = v->work[--v->work_count];
    if (!step(v, pc, v->depth_at[pc])) return false;
  }
  return true;
}

bool moth_verify(moth_vm *vm, char *err, size_t err_len) {
  uint32_t longest = 0;
  for (uint16_t i = 0; i < vm->nfuncs; i++) {
    if (vm->funcs[i].code_len > longest) longest = vm->funcs[i].code_len;
  }
  if (longest == 0) return true;

  verifier v;
  memset(&v, 0, sizeof v);
  v.vm = vm;
  v.depth_at = malloc((size_t)longest * sizeof *v.depth_at);
  v.work = malloc((size_t)longest * sizeof *v.work);
  if (!v.depth_at || !v.work) {
    free(v.depth_at);
    free(v.work);
    snprintf(err, err_len, "out of memory verifying the program");
    return false;
  }

  bool ok = true;
  for (uint16_t i = 0; i < vm->nfuncs && ok; i++) {
    if (!verify_function(&v, &vm->funcs[i])) {
      moth_str name = {"?", 1};
      if (vm->funcs[i].name_const < vm->nconsts &&
          vm->const_strs[vm->funcs[i].name_const].chars) {
        name = vm->const_strs[vm->funcs[i].name_const];
      }
      snprintf(err, err_len, "%.*s: %s", (int)name.len, name.chars, v.err);
      ok = false;
    }
  }

  free(v.depth_at);
  free(v.work);
  return ok;
}
