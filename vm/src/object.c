/* Heap objects and the garbage collector.
 *
 * Mark-sweep over a singly-linked list of every live object. Roots are the
 * value stack, the globals table, and the constant pool. There is no
 * compaction and no generational nursery yet — measure before adding either.
 */
#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MOTH_GC_INITIAL
#define MOTH_GC_INITIAL (32 * 1024)
#endif
#ifndef MOTH_GC_GROWTH
#define MOTH_GC_GROWTH 2
#endif

static moth_obj *allocate_object(moth_vm *vm, size_t size, obj_type type) {
  if (vm->gc_enabled && vm->bytes_allocated + size > vm->next_gc) {
    moth_collect(vm);
  }
  moth_obj *o = calloc(1, size);
  if (!o) return NULL;
  o->type = type;
  o->marked = false;
  o->next = vm->objects;
  vm->objects = o;
  vm->bytes_allocated += size;
  return o;
}

static moth_value make_string(moth_vm *vm, const char *chars, uint32_t len, bool owns) {
  moth_string *s = (moth_string *)allocate_object(vm, sizeof(moth_string), OBJ_STRING);
  if (!s) {
    if (owns) free((char *)chars);
    return moth_null();
  }
  s->len = len;
  s->chars = chars;
  s->owns_chars = owns;
  if (owns) vm->bytes_allocated += len;
  moth_value v;
  v.type = MV_OBJ;
  v.as.obj = (moth_obj *)s;
  return v;
}

moth_value moth_string_take(moth_vm *vm, char *chars, uint32_t len) {
  return make_string(vm, chars, len, true);
}

moth_value moth_string_borrow(moth_vm *vm, const char *chars, uint32_t len) {
  return make_string(vm, chars, len, false);
}

moth_value moth_new_string(moth_vm *vm, const char *chars, int len) {
  if (len < 0) len = 0;
  char *copy = malloc((size_t)len ? (size_t)len : 1);
  if (!copy) return moth_null();
  memcpy(copy, chars, (size_t)len);
  return moth_string_take(vm, copy, (uint32_t)len);
}

bool moth_is_string(moth_value v) { return IS_OBJ_TYPE(v, OBJ_STRING); }

const char *moth_string_chars(moth_value v, int *len_out) {
  if (!moth_is_string(v)) {
    if (len_out) *len_out = 0;
    return NULL;
  }
  moth_string *s = AS_STRING(v);
  if (len_out) *len_out = (int)s->len;
  return s->chars;
}

bool moth_string_equal(moth_value a, moth_value b) {
  moth_string *x = AS_STRING(a), *y = AS_STRING(b);
  return x->len == y->len && memcmp(x->chars, y->chars, x->len) == 0;
}

moth_value moth_concat(moth_vm *vm, moth_value a, moth_value b) {
  moth_string *x = AS_STRING(a), *y = AS_STRING(b);
  uint32_t len = x->len + y->len;
  char *chars = malloc(len ? len : 1);
  if (!chars) return moth_null();
  memcpy(chars, x->chars, x->len);
  memcpy(chars + x->len, y->chars, y->len);
  return moth_string_take(vm, chars, len);
}

/* Dart's own formatting: doubles keep a decimal point, ints never gain one. */
moth_value moth_to_string(moth_vm *vm, moth_value v) {
  char buf[40];
  int n = 0;
  switch (v.type) {
    case MV_NULL: n = snprintf(buf, sizeof buf, "null"); break;
    case MV_BOOL: n = snprintf(buf, sizeof buf, v.as.b ? "true" : "false"); break;
    case MV_INT: n = snprintf(buf, sizeof buf, "%" PRId64, v.as.i); break;
    case MV_DOUBLE:
      n = snprintf(buf, sizeof buf, "%g", v.as.d);
      if (!strpbrk(buf, ".einf")) n += snprintf(buf + n, sizeof buf - (size_t)n, ".0");
      break;
    case MV_OBJ:
      if (moth_is_string(v)) return v; /* already a string */
      n = snprintf(buf, sizeof buf, "Instance");
      break;
  }
  return moth_new_string(vm, buf, n);
}

/* ---- collector --------------------------------------------------------- */

static void mark_object(moth_obj *o) {
  if (!o || o->marked) return;
  o->marked = true;
  /* Strings reference nothing. Lists and instances will recurse here. */
}

static void mark_value(moth_value v) {
  if (v.type == MV_OBJ) mark_object(v.as.obj);
}

static void mark_roots(moth_vm *vm) {
  for (moth_value *slot = vm->stack; slot < vm->sp; slot++) mark_value(*slot);
  for (uint16_t i = 0; i < vm->nglobals; i++) mark_value(vm->globals[i]);
  /* Constant strings live on the heap but must outlive every collection. */
  for (uint16_t i = 0; i < vm->nconsts; i++) mark_value(vm->consts[i]);
}

static void free_object(moth_vm *vm, moth_obj *o) {
  switch (o->type) {
    case OBJ_STRING: {
      moth_string *s = (moth_string *)o;
      if (s->owns_chars) {
        vm->bytes_allocated -= s->len;
        free((char *)s->chars);
      }
      vm->bytes_allocated -= sizeof(moth_string);
      break;
    }
  }
  free(o);
}

static void sweep(moth_vm *vm) {
  moth_obj **link = &vm->objects;
  while (*link) {
    moth_obj *o = *link;
    if (o->marked) {
      o->marked = false;
      link = &o->next;
    } else {
      *link = o->next;
      free_object(vm, o);
    }
  }
}

void moth_collect(moth_vm *vm) {
  mark_roots(vm);
  sweep(vm);
  vm->next_gc = vm->bytes_allocated * MOTH_GC_GROWTH;
  if (vm->next_gc < MOTH_GC_INITIAL) vm->next_gc = MOTH_GC_INITIAL;
}

void moth_free_objects(moth_vm *vm) {
  moth_obj *o = vm->objects;
  while (o) {
    moth_obj *next = o->next;
    free_object(vm, o);
    o = next;
  }
  vm->objects = NULL;
}
