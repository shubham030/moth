/* .mothb blob parsing and native resolution. */
#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const uint8_t *p;
  const uint8_t *end;
  bool ok;
} reader;

static uint8_t rd_u8(reader *r) {
  if (r->p + 1 > r->end) { r->ok = false; return 0; }
  return *r->p++;
}
static uint16_t rd_u16(reader *r) {
  if (r->p + 2 > r->end) { r->ok = false; return 0; }
  uint16_t v = (uint16_t)(r->p[0] | (r->p[1] << 8));
  r->p += 2;
  return v;
}
static uint32_t rd_u32(reader *r) {
  if (r->p + 4 > r->end) { r->ok = false; return 0; }
  uint32_t v = (uint32_t)r->p[0] | ((uint32_t)r->p[1] << 8) | ((uint32_t)r->p[2] << 16) |
               ((uint32_t)r->p[3] << 24);
  r->p += 4;
  return v;
}
static int64_t rd_i64(reader *r) {
  if (r->p + 8 > r->end) { r->ok = false; return 0; }
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (uint64_t)r->p[i] << (8 * i);
  r->p += 8;
  return (int64_t)v;
}
static double rd_f64(reader *r) {
  int64_t bits = rd_i64(r);
  double d;
  memcpy(&d, &bits, sizeof d);
  return d;
}

static moth_status fail(moth_vm *vm, moth_status st, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->err, sizeof vm->err, fmt, ap);
  va_end(ap);
  return st;
}

void moth_register(moth_vm *vm, const char *name, moth_native_fn fn, void *user) {
  if (vm->nregs == vm->cap_regs) {
    int cap = vm->cap_regs ? vm->cap_regs * 2 : 16;
    moth_native_reg *n = realloc(vm->regs, (size_t)cap * sizeof *n);
    if (!n) return;
    vm->regs = n;
    vm->cap_regs = cap;
  }
  size_t len = strlen(name);
  char *copy = malloc(len + 1);
  if (!copy) return;
  memcpy(copy, name, len + 1);
  vm->regs[vm->nregs].name = copy;
  vm->regs[vm->nregs].fn = fn;
  vm->regs[vm->nregs].user = user;
  vm->nregs++;
}

static const moth_native_reg *find_reg(moth_vm *vm, moth_str name) {
  for (int i = 0; i < vm->nregs; i++) {
    if (strlen(vm->regs[i].name) == name.len &&
        memcmp(vm->regs[i].name, name.chars, name.len) == 0) {
      return &vm->regs[i];
    }
  }
  return NULL;
}

static uint16_t find_string_const(moth_vm *vm, const char *name) {
  size_t n = strlen(name);
  for (uint16_t i = 0; i < vm->nconsts; i++) {
    if (vm->const_strs[i].chars && vm->const_strs[i].len == n &&
        memcmp(vm->const_strs[i].chars, name, n) == 0) {
      return i;
    }
  }
  return 0xFFFF; /* not present in this program */
}

moth_status moth_load(moth_vm *vm, const uint8_t *blob, size_t len) {
  reader r = {blob, blob + len, true};

  if (len < 8 || memcmp(blob, "MOTH", 4) != 0) {
    return fail(vm, MOTH_ERR_FORMAT, "not a moth blob (bad magic)");
  }
  r.p += 4;
  uint16_t version = rd_u16(&r);
  if (version != MOTH_BYTECODE_VERSION) {
    return fail(vm, MOTH_ERR_FORMAT, "bytecode version %u, this VM speaks %u", version,
                MOTH_BYTECODE_VERSION);
  }
  rd_u16(&r); /* flags */

  /* constants */
  vm->nconsts = rd_u16(&r);
  if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated header");
  vm->consts = calloc(vm->nconsts ? vm->nconsts : 1, sizeof *vm->consts);
  vm->const_strs = calloc(vm->nconsts ? vm->nconsts : 1, sizeof *vm->const_strs);
  if (!vm->consts || !vm->const_strs) return fail(vm, MOTH_ERR_OOM, "out of memory");
  for (uint16_t i = 0; i < vm->nconsts; i++) {
    uint8_t tag = rd_u8(&r);
    switch (tag) {
      case CONST_INT: vm->consts[i] = moth_int(rd_i64(&r)); break;
      case CONST_DOUBLE: vm->consts[i] = moth_double(rd_f64(&r)); break;
      case CONST_BOOL: vm->consts[i] = moth_bool(rd_u8(&r) != 0); break;
      case CONST_NULL: vm->consts[i] = moth_null(); break;
      case CONST_STRING: {
        uint16_t slen = rd_u16(&r);
        if (!r.ok || r.p + slen > r.end) return fail(vm, MOTH_ERR_FORMAT, "truncated string const");
        vm->const_strs[i].chars = (const char *)r.p;
        vm->const_strs[i].len = slen;
        /* Borrows the blob's bytes — a constant string costs only a header. */
        vm->consts[i] = moth_string_borrow(vm, (const char *)r.p, slen);
        if (vm->consts[i].type != MV_OBJ) return fail(vm, MOTH_ERR_OOM, "out of memory");
        r.p += slen;
        break;
      }
      default: return fail(vm, MOTH_ERR_FORMAT, "unknown constant tag %u at %u", tag, i);
    }
    if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated constant %u", i);
  }

  /* natives — resolved against host registrations now, not at call time */
  vm->nnatives = rd_u16(&r);
  if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated native table");
  vm->natives = calloc(vm->nnatives ? vm->nnatives : 1, sizeof *vm->natives);
  if (!vm->natives) return fail(vm, MOTH_ERR_OOM, "out of memory");
  for (uint16_t i = 0; i < vm->nnatives; i++) {
    uint16_t name_c = rd_u16(&r);
    uint8_t argc = rd_u8(&r);
    if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated native %u", i);
    if (name_c >= vm->nconsts || vm->const_strs[name_c].chars == NULL) {
      return fail(vm, MOTH_ERR_FORMAT, "native %u has no name constant", i);
    }
    moth_str name = vm->const_strs[name_c];
    const moth_native_reg *reg = find_reg(vm, name);
    if (!reg) {
      return fail(vm, MOTH_ERR_UNRESOLVED_NATIVE,
                  "this program needs '%.*s', which this board does not provide",
                  (int)name.len, name.chars);
    }
    vm->natives[i].fn = reg->fn;
    vm->natives[i].user = reg->user;
    vm->natives[i].argc = argc;
  }

  /* top-level variables — one slot each, all starting null */
  vm->nglobals = rd_u16(&r);
  if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated globals count");
  if (vm->nglobals > 0) {
    vm->globals = calloc(vm->nglobals, sizeof *vm->globals);
    if (!vm->globals) return fail(vm, MOTH_ERR_OOM, "out of memory");
    for (uint16_t i = 0; i < vm->nglobals; i++) vm->globals[i] = moth_null();
  }

  /* classes */
  vm->nclasses = rd_u16(&r);
  if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated class table");
  if (vm->nclasses > 0) {
    vm->classes = calloc(vm->nclasses, sizeof *vm->classes);
    if (!vm->classes) return fail(vm, MOTH_ERR_OOM, "out of memory");
  }
  for (uint16_t i = 0; i < vm->nclasses; i++) {
    moth_class *c = &vm->classes[i];
    c->name_const = rd_u16(&r);
    c->nfields = rd_u8(&r);
    if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated class %u", i);
    if (c->nfields > 0) {
      c->field_names = calloc(c->nfields, sizeof *c->field_names);
      if (!c->field_names) return fail(vm, MOTH_ERR_OOM, "out of memory");
      for (uint8_t f = 0; f < c->nfields; f++) c->field_names[f] = rd_u16(&r);
    }
    c->nmethods = rd_u16(&r);
    if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated class %u", i);
    if (c->nmethods > 0) {
      c->methods = calloc(c->nmethods, sizeof *c->methods);
      if (!c->methods) return fail(vm, MOTH_ERR_OOM, "out of memory");
      for (uint16_t m = 0; m < c->nmethods; m++) {
        c->methods[m].name_const = rd_u16(&r);
        c->methods[m].func_index = rd_u16(&r);
        c->methods[m].member_kind = rd_u8(&r);
      }
    }
    c->ctor = rd_u16(&r);
    if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated class %u", i);
  }

  /* functions */
  vm->nfuncs = rd_u16(&r);
  if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated function table");
  vm->funcs = calloc(vm->nfuncs ? vm->nfuncs : 1, sizeof *vm->funcs);
  if (!vm->funcs) return fail(vm, MOTH_ERR_OOM, "out of memory");
  for (uint16_t i = 0; i < vm->nfuncs; i++) {
    moth_func *f = &vm->funcs[i];
    f->name_const = rd_u16(&r);
    f->arity = rd_u8(&r);
    f->nlocals = rd_u8(&r);
    f->code_len = rd_u32(&r);
    if (!r.ok || r.p + f->code_len > r.end) {
      return fail(vm, MOTH_ERR_FORMAT, "truncated function %u", i);
    }
    if (f->nlocals < f->arity) {
      return fail(vm, MOTH_ERR_FORMAT, "function %u: nlocals < arity", i);
    }
    f->code = r.p;
    r.p += f->code_len;
  }

  vm->entry = rd_u16(&r);
  vm->init = rd_u16(&r);
  if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated entry index");
  if (vm->entry >= vm->nfuncs) return fail(vm, MOTH_ERR_FORMAT, "entry index out of range");
  if (vm->funcs[vm->entry].arity != 0) {
    return fail(vm, MOTH_ERR_FORMAT, "entry function must take no arguments");
  }
  if (vm->init != MOTH_NO_INIT && vm->init >= vm->nfuncs) {
    return fail(vm, MOTH_ERR_FORMAT, "initializer index out of range");
  }

  /* The class table is read before the function table, so its function
   * references can only be checked now. OP_INVOKE indexes vm->funcs with
   * these directly — an unchecked value would read out of bounds and then
   * execute through a garbage code pointer. */
  for (uint16_t i = 0; i < vm->nclasses; i++) {
    moth_class *c = &vm->classes[i];
    if (c->ctor != MOTH_NO_CTOR) {
      if (c->ctor >= vm->nfuncs) {
        return fail(vm, MOTH_ERR_FORMAT, "class %u: constructor index out of range", i);
      }
      if (vm->funcs[c->ctor].arity < 1) {
        return fail(vm, MOTH_ERR_FORMAT, "class %u: constructor has no receiver slot", i);
      }
    }
    for (uint16_t m = 0; m < c->nmethods; m++) {
      if (c->methods[m].func_index >= vm->nfuncs) {
        return fail(vm, MOTH_ERR_FORMAT, "class %u: method %u index out of range", i, m);
      }
      /* slot 0 is the receiver, so a method always takes at least one slot */
      if (vm->funcs[c->methods[m].func_index].arity < 1) {
        return fail(vm, MOTH_ERR_FORMAT, "class %u: method %u has no receiver slot", i, m);
      }
    }
  }

  /* assets — raster images the compiler embedded. Pixels are borrowed from
   * the blob, never copied: on a flash-mapped blob an image costs no RAM.
   * Every count and offset is validated here, because this section arrives
   * over the network like the rest of the blob. */
  vm->nassets = rd_u16(&r);
  if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated asset table");
  if (vm->nassets > 0) {
    vm->assets = calloc(vm->nassets, sizeof *vm->assets);
    if (!vm->assets) return fail(vm, MOTH_ERR_OOM, "out of memory");
    for (uint16_t i = 0; i < vm->nassets; i++) {
      moth_asset_rec *a = &vm->assets[i];
      a->key_const = rd_u16(&r);
      a->w = rd_u16(&r);
      a->h = rd_u16(&r);
      if (!r.ok) return fail(vm, MOTH_ERR_FORMAT, "truncated asset %u", i);
      if (a->key_const >= vm->nconsts || vm->const_strs[a->key_const].len == 0) {
        return fail(vm, MOTH_ERR_FORMAT, "asset %u: key is not a string constant", i);
      }
      if (a->w == 0 || a->h == 0 || a->w > 2048 || a->h > 2048) {
        return fail(vm, MOTH_ERR_FORMAT, "asset %u: %ux%u is not a sane size", i,
                    a->w, a->h);
      }
      /* Pixels are 4-byte aligned FROM THE BLOB START (the writer pads),
       * so a host that maps or allocates the blob 4-aligned — both do —
       * can read u32s directly. */
      size_t off = (size_t)(r.p - blob);
      size_t pad = (4 - (off & 3)) & 3;
      if (r.p + pad > r.end) return fail(vm, MOTH_ERR_FORMAT, "truncated asset %u", i);
      r.p += pad;
      size_t npix = (size_t)a->w * a->h;
      if ((size_t)(r.end - r.p) < npix * 4) {
        return fail(vm, MOTH_ERR_FORMAT, "asset %u: pixel data truncated", i);
      }
      a->pixels = (const uint32_t *)r.p;
      r.p += npix * 4;
    }
  }

  /* Cache the built-in member names so property access compares integers.
   * 0xFFFF means the program never mentions that name, which can never match. */
  vm->k_length = find_string_const(vm, "length");
  vm->k_add = find_string_const(vm, "add");
  vm->k_remove_last = find_string_const(vm, "removeLast");
  vm->k_clear = find_string_const(vm, "clear");

  /* Everything above checks the tables; this checks the code itself, so a
   * blob arriving over the network cannot reach the interpreter unbalanced. */
  char verr[160];
  if (!moth_verify(vm, verr, sizeof verr)) {
    return fail(vm, MOTH_ERR_FORMAT, "%s", verr);
  }

  vm->blob = blob;
  vm->blob_len = len;
  vm->loaded = true;
  vm->err[0] = '\0';
  vm->gc_enabled = true; /* the constant table is complete and rootable now */
  return MOTH_OK;
}

int moth_asset_count(const moth_vm *vm) { return vm && vm->loaded ? vm->nassets : 0; }

bool moth_asset_info(const moth_vm *vm, int i, const char **key, size_t *key_len,
                     int *w, int *h, const uint32_t **pixels) {
  if (!vm || !vm->loaded || i < 0 || i >= vm->nassets) return false;
  const moth_asset_rec *a = &vm->assets[i];
  const moth_str *k = &vm->const_strs[a->key_const];
  if (key) *key = k->chars;
  if (key_len) *key_len = k->len;
  if (w) *w = a->w;
  if (h) *h = a->h;
  if (pixels) *pixels = a->pixels;
  return true;
}

moth_vm *moth_new(void) {
  moth_vm *vm = calloc(1, sizeof *vm);
  if (vm) {
    vm->sp = vm->stack;
    vm->next_gc = 32 * 1024;
  }
  return vm;
}

void moth_free(moth_vm *vm) {
  if (!vm) return;
  moth_free_objects(vm);
  for (int i = 0; i < vm->nregs; i++) free((char *)vm->regs[i].name);
  free(vm->regs);
  free(vm->consts);
  free(vm->const_strs);
  free(vm->natives);
  free(vm->funcs);
  free(vm->globals);
  free(vm->assets);
  for (uint16_t i = 0; i < vm->nclasses; i++) {
    free(vm->classes[i].field_names);
    free(vm->classes[i].methods);
  }
  free(vm->classes);
  free(vm);
}

const char *moth_error(const moth_vm *vm) { return vm ? vm->err : ""; }
