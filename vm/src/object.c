/* Heap objects and the garbage collector.
 *
 * Mark-sweep over a singly-linked list of every live object. Roots are the
 * value stack, the globals table, and the constant pool. There is no
 * compaction and no generational nursery yet — measure before adding either.
 */
#include "internal.h"

#include <inttypes.h>
#include <math.h>
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

moth_value moth_list_new(moth_vm *vm) {
  moth_list *l = (moth_list *)allocate_object(vm, sizeof(moth_list), OBJ_LIST);
  if (!l) return moth_null();
  l->count = 0;
  l->capacity = 0;
  l->items = NULL;
  moth_value v;
  v.type = MV_OBJ;
  v.as.obj = (moth_obj *)l;
  return v;
}

bool moth_list_push(moth_vm *vm, moth_value list, moth_value item) {
  moth_list *l = AS_LIST(list);
  if (l->count == l->capacity) {
    int cap = l->capacity < 8 ? 8 : l->capacity * 2;
    moth_value *items = realloc(l->items, (size_t)cap * sizeof(moth_value));
    if (!items) return false;
    vm->bytes_allocated += (size_t)(cap - l->capacity) * sizeof(moth_value);
    l->items = items;
    l->capacity = cap;
  }
  l->items[l->count++] = item;
  return true;
}

moth_value moth_instance_new(moth_vm *vm, uint16_t class_index, uint8_t nfields) {
  moth_instance *inst =
      (moth_instance *)allocate_object(vm, sizeof(moth_instance), OBJ_INSTANCE);
  if (!inst) return moth_null();
  inst->class_index = class_index;
  inst->fields = NULL;
  if (nfields > 0) {
    inst->fields = malloc((size_t)nfields * sizeof(moth_value));
    if (!inst->fields) return moth_null();
    for (uint8_t i = 0; i < nfields; i++) inst->fields[i] = moth_null();
    vm->bytes_allocated += (size_t)nfields * sizeof(moth_value);
  }
  moth_value v;
  v.type = MV_OBJ;
  v.as.obj = (moth_obj *)inst;
  return v;
}

/* ---- Dart-compatible formatting ---------------------------------------- */

/* Dart prints the shortest decimal that reads back as the same double, so
 * 0.1 + 0.2 shows all 17 digits while 0.5 shows one. Plain "%g" would round
 * to 6 significant digits and disagree. */
int moth_format_double(char *buf, size_t n, double d) {
  if (isnan(d)) return snprintf(buf, n, "NaN");
  if (isinf(d)) return snprintf(buf, n, d < 0 ? "-Infinity" : "Infinity");

  /* Fewest significant digits that read back as the same double. */
  char tmp[64];
  int digits = 1;
  for (; digits < 17; digits++) {
    snprintf(tmp, sizeof tmp, "%.*e", digits - 1, d);
    if (strtod(tmp, NULL) == d) break;
  }
  snprintf(tmp, sizeof tmp, "%.*e", digits - 1, d);

  const char *marker = strchr(tmp, 'e');
  int exp10 = marker ? atoi(marker + 1) : 0;

  /* Dart (like JavaScript) writes plain decimals for exponents in [-6, 21)
   * and scientific notation outside it. "%g" uses a different rule and would
   * turn 10.0 into 1e+01. */
  int len;
  if (exp10 >= -6 && exp10 < 21) {
    int decimals = digits - 1 - exp10;
    if (decimals < 0) decimals = 0;
    len = snprintf(buf, n, "%.*f", decimals, d);
    if (!strchr(buf, '.') && (size_t)len + 3 < n) {
      len += snprintf(buf + len, n - (size_t)len, ".0"); /* 1.0, never 1 */
    }
  } else {
    char mantissa[32];
    size_t take = marker ? (size_t)(marker - tmp) : strlen(tmp);
    if (take >= sizeof mantissa) take = sizeof mantissa - 1;
    memcpy(mantissa, tmp, take);
    mantissa[take] = '\0';
    /* Dart writes e+21 and e-7 — always signed, never zero-padded */
    len = snprintf(buf, n, "%se%+d", mantissa, exp10);
  }
  return len;
}

typedef struct {
  char *data;
  size_t len, cap;
  bool failed;
} strbuf;

static void sb_add(strbuf *sb, const char *text, size_t n) {
  if (sb->failed) return;
  if (sb->len + n > sb->cap) {
    size_t cap = sb->cap ? sb->cap * 2 : 64;
    while (cap < sb->len + n) cap *= 2;
    char *grown = realloc(sb->data, cap);
    if (!grown) { sb->failed = true; return; }
    sb->data = grown;
    sb->cap = cap;
  }
  memcpy(sb->data + sb->len, text, n);
  sb->len += n;
}

#define MOTH_FORMAT_MAX_DEPTH 24

/* Appends v the way Dart's print() would. Strings nested inside a list are
 * printed bare, as Dart does. The vm is needed to name an instance's class. */
static void format_value(moth_vm *vm, strbuf *sb, moth_value v, int depth) {
  char scratch[40];
  switch (v.type) {
    case MV_NULL: sb_add(sb, "null", 4); return;
    case MV_BOOL:
      if (v.as.b) sb_add(sb, "true", 4);
      else sb_add(sb, "false", 5);
      return;
    case MV_INT:
      sb_add(sb, scratch, (size_t)snprintf(scratch, sizeof scratch, "%" PRId64, v.as.i));
      return;
    case MV_DOUBLE:
      sb_add(sb, scratch, (size_t)moth_format_double(scratch, sizeof scratch, v.as.d));
      return;
    case MV_OBJ: break;
  }

  if (moth_is_string(v)) {
    sb_add(sb, AS_STRING(v)->chars, AS_STRING(v)->len);
    return;
  }
  if (IS_LIST(v)) {
    /* A list that contains itself would recurse forever; Dart prints [...] */
    if (depth >= MOTH_FORMAT_MAX_DEPTH) {
      sb_add(sb, "[...]", 5);
      return;
    }
    moth_list *l = AS_LIST(v);
    sb_add(sb, "[", 1);
    for (int i = 0; i < l->count; i++) {
      if (i > 0) sb_add(sb, ", ", 2);
      format_value(vm, sb, l->items[i], depth + 1);
    }
    sb_add(sb, "]", 1);
    return;
  }
  if (IS_INSTANCE(v)) {
    /* Dart writes Instance of 'Point'; the class name is already in the
     * constant pool, reached through the instance's class index. */
    uint16_t cls = AS_INSTANCE(v)->class_index;
    if (vm && cls < vm->nclasses) {
      uint16_t name_const = vm->classes[cls].name_const;
      if (name_const < vm->nconsts && vm->const_strs[name_const].chars) {
        sb_add(sb, "Instance of '", 13);
        sb_add(sb, vm->const_strs[name_const].chars, vm->const_strs[name_const].len);
        sb_add(sb, "'", 1);
        return;
      }
    }
  }
  sb_add(sb, "Instance", 8);
}

moth_value moth_to_string(moth_vm *vm, moth_value v) {
  if (moth_is_string(v)) return v; /* already a string */

  strbuf sb = {NULL, 0, 0, false};
  format_value(vm, &sb, v, 0);
  if (sb.failed) {
    free(sb.data);
    return moth_null();
  }
  moth_value out = moth_new_string(vm, sb.data ? sb.data : "", (int)sb.len);
  free(sb.data);
  return out;
}

/* ---- collector --------------------------------------------------------- */

/* Instances need their class's field count to be traced, and mark_object has
 * no vm parameter. The collector is single-threaded and non-reentrant, so a
 * file-scope handle set for the duration of a collection is sufficient. */
static moth_vm *g_marking_vm;

static void mark_value(moth_value v);

static void mark_object(moth_obj *o) {
  if (!o || o->marked) return;
  o->marked = true; /* set before recursing, so cycles terminate */
  switch (o->type) {
    case OBJ_STRING:
      break; /* references nothing */
    case OBJ_LIST: {
      moth_list *l = (moth_list *)o;
      for (int i = 0; i < l->count; i++) mark_value(l->items[i]);
      break;
    }
    case OBJ_INSTANCE: {
      moth_instance *inst = (moth_instance *)o;
      /* the field count lives on the class, reached through the owning vm */
      if (inst->fields && g_marking_vm && inst->class_index < g_marking_vm->nclasses) {
        uint8_t n = g_marking_vm->classes[inst->class_index].nfields;
        for (uint8_t i = 0; i < n; i++) mark_value(inst->fields[i]);
      }
      break;
    }
  }
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
    case OBJ_LIST: {
      moth_list *l = (moth_list *)o;
      vm->bytes_allocated -= (size_t)l->capacity * sizeof(moth_value);
      vm->bytes_allocated -= sizeof(moth_list);
      free(l->items);
      break;
    }
    case OBJ_INSTANCE: {
      moth_instance *inst = (moth_instance *)o;
      if (inst->fields && inst->class_index < vm->nclasses) {
        vm->bytes_allocated -=
            (size_t)vm->classes[inst->class_index].nfields * sizeof(moth_value);
      }
      vm->bytes_allocated -= sizeof(moth_instance);
      free(inst->fields);
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
  g_marking_vm = vm;
  mark_roots(vm);
  g_marking_vm = NULL;
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
