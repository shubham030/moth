/* The interpreter loop. Switch dispatch (ADR-003) — revisit with profiles
 * on hardware, not before. */
#include "internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static moth_status trap(moth_vm *vm, moth_frame *fr, moth_status st, const char *fmt, ...) {
  char msg[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  moth_str name = {"?", 1};
  if (fr && fr->fn->name_const < vm->nconsts && vm->const_strs[fr->fn->name_const].chars) {
    name = vm->const_strs[fr->fn->name_const];
  }
  snprintf(vm->err, sizeof vm->err, "%s (in %.*s, pc %ld)", msg, (int)name.len, name.chars,
           fr ? (long)(fr->ip - fr->fn->code) : -1L);
  return st;
}

#define PUSH(v)                                                      \
  do {                                                               \
    if (vm->sp >= vm->stack + MOTH_STACK_MAX)                        \
      return trap(vm, fr, MOTH_ERR_STACK_OVERFLOW, "value stack overflow"); \
    *vm->sp++ = (v);                                                 \
  } while (0)
#define POP() (*--vm->sp)
#define PEEK() (vm->sp[-1])

/* Operand reads must stay inside the function's code. Cheap next to the
 * dispatch switch, and the difference between a clear error and a buffer
 * over-read once hot-push starts accepting blobs off the network. */
#define NEED(n)                                                       \
  do {                                                                \
    if (fr->ip + (n) > fr->fn->code + fr->fn->code_len)               \
      return trap(vm, fr, MOTH_ERR_BAD_OP, "truncated instruction");  \
  } while (0)

static uint16_t read_u16(const uint8_t **ip) {
  uint16_t v = (uint16_t)((*ip)[0] | ((*ip)[1] << 8));
  *ip += 2;
  return v;
}
static int16_t read_i16(const uint8_t **ip) { return (int16_t)read_u16(ip); }

/* Dart's % is Euclidean: the result is always in [0, b.abs()), unlike C's
 * remainder which takes the sign of the dividend. */
static int64_t dart_mod_int(int64_t a, int64_t b) {
  int64_t r = a % b;
  if (r < 0) r += (b < 0) ? -b : b;
  return r;
}
static double dart_mod_double(double a, double b) {
  double r = fmod(a, b);
  if (r < 0) r += (b < 0) ? -b : b;
  return r;
}

static moth_status run_function(moth_vm *vm, uint16_t fn_index) {
  vm->sp = vm->stack;
  vm->nframes = 0;

  moth_frame *fr = &vm->frames[vm->nframes++];
  fr->fn = &vm->funcs[fn_index];
  fr->ip = fr->fn->code;
  fr->slots = vm->sp;
  for (int i = 0; i < fr->fn->nlocals; i++) PUSH(moth_null());

  for (;;) {
    if (fr->ip < fr->fn->code || fr->ip >= fr->fn->code + fr->fn->code_len) {
      return trap(vm, fr, MOTH_ERR_BAD_OP, "ran off the end of the function");
    }
    uint8_t op = *fr->ip++;

    switch (op) {
      case OP_NOP: break;

      case OP_CONST: {
        NEED(2);
        uint16_t idx = read_u16(&fr->ip);
        if (idx >= vm->nconsts) return trap(vm, fr, MOTH_ERR_BAD_OP, "constant out of range");
        PUSH(vm->consts[idx]);
        break;
      }
      case OP_INT8: NEED(1); PUSH(moth_int((int8_t)*fr->ip++)); break;
      case OP_TRUE: PUSH(moth_bool(true)); break;
      case OP_FALSE: PUSH(moth_bool(false)); break;
      case OP_NULL: PUSH(moth_null()); break;
      case OP_POP: (void)POP(); break;
      case OP_DUP: { moth_value v = PEEK(); PUSH(v); break; }

      case OP_LOAD: {
        NEED(1);
        uint8_t slot = *fr->ip++;
        if (slot >= fr->fn->nlocals) return trap(vm, fr, MOTH_ERR_BAD_OP, "local out of range");
        PUSH(fr->slots[slot]);
        break;
      }
      case OP_STORE: {
        NEED(1);
        uint8_t slot = *fr->ip++;
        if (slot >= fr->fn->nlocals) return trap(vm, fr, MOTH_ERR_BAD_OP, "local out of range");
        fr->slots[slot] = POP();
        break;
      }
      case OP_LOAD_GLOBAL: {
        NEED(2);
        uint16_t slot = read_u16(&fr->ip);
        if (slot >= vm->nglobals) return trap(vm, fr, MOTH_ERR_BAD_OP, "global out of range");
        PUSH(vm->globals[slot]);
        break;
      }
      case OP_STORE_GLOBAL: {
        NEED(2);
        uint16_t slot = read_u16(&fr->ip);
        if (slot >= vm->nglobals) return trap(vm, fr, MOTH_ERR_BAD_OP, "global out of range");
        vm->globals[slot] = POP();
        break;
      }

      case OP_ADD: case OP_SUB: case OP_MUL: case OP_IDIV: case OP_MOD: {
        moth_value b = POP(), a = POP();
        if (!moth_is_num(a) || !moth_is_num(b)) {
          return trap(vm, fr, MOTH_ERR_TYPE, "arithmetic needs numbers");
        }
        bool ints = a.type == MV_INT && b.type == MV_INT;
        if (ints) {
          int64_t x = a.as.i, y = b.as.i;
          switch (op) {
            case OP_ADD: PUSH(moth_int(x + y)); break;
            case OP_SUB: PUSH(moth_int(x - y)); break;
            case OP_MUL: PUSH(moth_int(x * y)); break;
            case OP_IDIV:
              if (y == 0) return trap(vm, fr, MOTH_ERR_DIV_ZERO, "integer division by zero");
              PUSH(moth_int(x / y));
              break;
            case OP_MOD:
              if (y == 0) return trap(vm, fr, MOTH_ERR_DIV_ZERO, "modulo by zero");
              PUSH(moth_int(dart_mod_int(x, y)));
              break;
          }
        } else {
          double x = moth_as_double(a), y = moth_as_double(b);
          switch (op) {
            case OP_ADD: PUSH(moth_double(x + y)); break;
            case OP_SUB: PUSH(moth_double(x - y)); break;
            case OP_MUL: PUSH(moth_double(x * y)); break;
            case OP_IDIV:
              if (y == 0) return trap(vm, fr, MOTH_ERR_DIV_ZERO, "integer division by zero");
              PUSH(moth_int((int64_t)(x / y)));
              break;
            case OP_MOD: PUSH(moth_double(dart_mod_double(x, y))); break;
          }
        }
        break;
      }
      case OP_DIV: { /* Dart's / is always double */
        moth_value b = POP(), a = POP();
        if (!moth_is_num(a) || !moth_is_num(b)) {
          return trap(vm, fr, MOTH_ERR_TYPE, "arithmetic needs numbers");
        }
        PUSH(moth_double(moth_as_double(a) / moth_as_double(b)));
        break;
      }
      case OP_NEG: {
        moth_value a = POP();
        if (a.type == MV_INT) PUSH(moth_int(-a.as.i));
        else if (a.type == MV_DOUBLE) PUSH(moth_double(-a.as.d));
        else return trap(vm, fr, MOTH_ERR_TYPE, "unary - needs a number");
        break;
      }

      case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
        moth_value b = POP(), a = POP();
        if (a.type != MV_INT || b.type != MV_INT) {
          return trap(vm, fr, MOTH_ERR_TYPE, "bitwise operators need whole numbers");
        }
        int64_t x = a.as.i, y = b.as.i;
        switch (op) {
          case OP_BAND: PUSH(moth_int(x & y)); break;
          case OP_BOR: PUSH(moth_int(x | y)); break;
          case OP_BXOR: PUSH(moth_int(x ^ y)); break;
          case OP_SHL:
            if (y < 0) return trap(vm, fr, MOTH_ERR_TYPE, "cannot shift by a negative amount");
            /* C leaves shifts >= width undefined; Dart just runs off the end */
            PUSH(moth_int(y >= 64 ? 0 : (int64_t)((uint64_t)x << y)));
            break;
          case OP_SHR:
            if (y < 0) return trap(vm, fr, MOTH_ERR_TYPE, "cannot shift by a negative amount");
            PUSH(moth_int(y >= 64 ? (x < 0 ? -1 : 0) : (x >> y)));
            break;
        }
        break;
      }
      case OP_BNOT: {
        moth_value a = POP();
        if (a.type != MV_INT) return trap(vm, fr, MOTH_ERR_TYPE, "~ needs a whole number");
        PUSH(moth_int(~a.as.i));
        break;
      }

      case OP_EQ: case OP_NE: {
        moth_value b = POP(), a = POP();
        bool eq;
        if (moth_is_num(a) && moth_is_num(b)) {
          eq = (a.type == MV_INT && b.type == MV_INT) ? a.as.i == b.as.i
                                                      : moth_as_double(a) == moth_as_double(b);
        } else if (a.type != b.type) {
          eq = false;
        } else {
          eq = a.type == MV_NULL ? true : a.as.b == b.as.b;
        }
        PUSH(moth_bool(op == OP_EQ ? eq : !eq));
        break;
      }
      case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        moth_value b = POP(), a = POP();
        if (!moth_is_num(a) || !moth_is_num(b)) {
          return trap(vm, fr, MOTH_ERR_TYPE, "comparison needs numbers");
        }
        bool res;
        if (a.type == MV_INT && b.type == MV_INT) {
          int64_t x = a.as.i, y = b.as.i;
          res = op == OP_LT ? x < y : op == OP_LE ? x <= y : op == OP_GT ? x > y : x >= y;
        } else {
          double x = moth_as_double(a), y = moth_as_double(b);
          res = op == OP_LT ? x < y : op == OP_LE ? x <= y : op == OP_GT ? x > y : x >= y;
        }
        PUSH(moth_bool(res));
        break;
      }
      case OP_NOT: {
        moth_value a = POP();
        if (a.type != MV_BOOL) return trap(vm, fr, MOTH_ERR_TYPE, "! needs a bool");
        PUSH(moth_bool(!a.as.b));
        break;
      }

      case OP_JUMP: { NEED(2); int16_t off = read_i16(&fr->ip); fr->ip += off; break; }
      case OP_JUMP_IF_FALSE: {
        NEED(2);
        int16_t off = read_i16(&fr->ip);
        moth_value c = POP();
        if (c.type != MV_BOOL) {
          return trap(vm, fr, MOTH_ERR_TYPE, "condition must be a bool (Dart has no truthiness)");
        }
        if (!c.as.b) fr->ip += off;
        break;
      }
      case OP_JUMP_IF_FALSE_K: case OP_JUMP_IF_TRUE_K: {
        NEED(2);
        int16_t off = read_i16(&fr->ip);
        moth_value c = PEEK();
        if (c.type != MV_BOOL) {
          return trap(vm, fr, MOTH_ERR_TYPE, "condition must be a bool");
        }
        if (c.as.b == (op == OP_JUMP_IF_TRUE_K)) fr->ip += off;
        break;
      }

      case OP_CALL: {
        NEED(3);
        uint16_t fidx = read_u16(&fr->ip);
        uint8_t argc = *fr->ip++;
        if (fidx >= vm->nfuncs) return trap(vm, fr, MOTH_ERR_BAD_OP, "call to unknown function");
        const moth_func *callee = &vm->funcs[fidx];
        if (argc != callee->arity) {
          return trap(vm, fr, MOTH_ERR_BAD_OP, "wrong argument count");
        }
        if (vm->nframes >= MOTH_FRAMES_MAX) {
          return trap(vm, fr, MOTH_ERR_STACK_OVERFLOW, "call stack overflow (infinite recursion?)");
        }
        moth_value *slots = vm->sp - argc;
        for (int i = argc; i < callee->nlocals; i++) PUSH(moth_null());
        moth_frame *nf = &vm->frames[vm->nframes++];
        nf->fn = callee;
        nf->ip = callee->code;
        nf->slots = slots;
        fr = nf;
        break;
      }
      case OP_NATIVE: {
        NEED(3);
        uint16_t nidx = read_u16(&fr->ip);
        uint8_t argc = *fr->ip++;
        if (nidx >= vm->nnatives) return trap(vm, fr, MOTH_ERR_BAD_OP, "unknown native");
        moth_native_slot *ns = &vm->natives[nidx];
        if (argc != ns->argc) return trap(vm, fr, MOTH_ERR_BAD_OP, "wrong native argument count");
        moth_value *argv = vm->sp - argc;
        moth_value res = ns->fn(argc, argv, ns->user);
        vm->sp = argv;
        PUSH(res);
        break;
      }
      case OP_RET: case OP_RET_NULL: {
        moth_value res = op == OP_RET ? POP() : moth_null();
        vm->nframes--;
        if (vm->nframes == 0) {
          vm->sp = vm->stack;
          return MOTH_OK;
        }
        vm->sp = fr->slots;
        fr = &vm->frames[vm->nframes - 1];
        PUSH(res);
        break;
      }

      default:
        return trap(vm, fr, MOTH_ERR_BAD_OP, "unknown opcode 0x%02x", op);
    }
  }
}

moth_status moth_run(moth_vm *vm) {
  if (!vm->loaded) {
    snprintf(vm->err, sizeof vm->err, "no program loaded");
    return MOTH_ERR_FORMAT;
  }
  vm->err[0] = '\0';

  /* Top-level variable initializers run before main, in declaration order. */
  if (vm->init != MOTH_NO_INIT) {
    moth_status st = run_function(vm, vm->init);
    if (st != MOTH_OK) return st;
  }
  return run_function(vm, vm->entry);
}
