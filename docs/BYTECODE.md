---
title: Bytecode format
sidebar_position: 10
slug: /bytecode
---

# moth bytecode

The compiler (`tools/mothc`, Dart) emits a `.mothb` blob; the VM (`vm/`, C)
loads and interprets it. This document is normative for both.

Design: **stack machine, byte-aligned, little-endian** (ADR-003). Simplicity
wins over density at this stage — the interpreter is not the bottleneck.

## Blob format

All multi-byte integers are little-endian.

```
magic        4 bytes   "MOTH"
version      u16       MOTH_BYTECODE_VERSION (currently 6)
flags        u16       reserved, 0
constants    u16 count, then each: tag u8 + payload
natives      u16 count, then each: name_const u16 + argc u8
globals      u16       number of top-level variable slots
classes      u16 count, then each:
               name_const  u16
               nfields     u8
               field_names nfields × u16 (constant indices)
               nmethods    u16, then each: name_const u16 + func_index u16
                           + member_kind u8 (0 method, 1 getter, 2 setter —
                           arity cannot tell a getter from a no-arg method)
               ctor        u16  constructor function index, or 0xFFFF
functions    u16 count, then each:
               name_const  u16
               arity       u8
               nlocals     u8    total slots (params occupy slots 0..arity-1)
               code_len    u32
               code        code_len bytes
entry        u16       index of the function to call at start (must be arity 0)
init         u16       initializer function index, or 0xFFFF for none
assets       u16 count, then each:
               key_const  u16   constant index of the string key
               width      u16   1..2048
               height     u16   1..2048
               pad        0..3 bytes so pixels start 4-byte aligned
                          from the blob's first byte
               pixels     width × height u32 ARGB8888, rows top to bottom
```

Global slots start as `null` and are filled by the `init` function, which the
VM runs to completion before `entry`. The compiler synthesizes it from the
top-level variable initializers, in declaration order.

Constant tags: `0 = int` (i64), `1 = double` (f64), `2 = string`
(u16 byte length + UTF-8, no terminator), `3 = bool` (u8), `4 = null`.

Strings appear in the pool for names even when the VM has no string *values*
yet (M1a) — they are used for native resolution and diagnostics.

Assets are how images travel: the board has no filesystem for user files, so
the compiler embeds decoded pixels and the VM lends them out by reference
(`moth_asset_info`). Nothing is copied — on a flash-mapped blob an image
costs no RAM. The alignment padding exists for exactly that pointer: hosts
map or allocate blobs at least 4-aligned, so aligning pixels to the blob
start makes them directly readable as `u32`s.

## Values

`null`, `bool`, `int` (64-bit signed), `double`, and heap objects: strings,
lists and class instances. Heap objects are garbage collected (mark-sweep);
constant strings borrow the blob's bytes and are never freed.

Fields and methods are looked up by **constant index**, not by string: the
pool deduplicates, so identical names always share one index and lookup is an
integer scan. Instance layout is the class's field order.

## Instruction set

Operands are shown after the mnemonic. Stack effect is written
`[before → after]`, rightmost is top of stack.

| Op | Byte | Operands | Effect |
| -- | ---- | -------- | ------ |
| `NOP` | 0x00 | — | `[→]` |
| `CONST` | 0x01 | u16 idx | `[→ v]` push constant |
| `INT8` | 0x02 | i8 | `[→ int]` push small immediate |
| `TRUE` | 0x03 | — | `[→ true]` |
| `FALSE` | 0x04 | — | `[→ false]` |
| `NULL` | 0x05 | — | `[→ null]` |
| `POP` | 0x06 | — | `[v →]` |
| `DUP` | 0x07 | — | `[v → v v]` |
| `LOAD` | 0x08 | u8 slot | `[→ v]` push local |
| `STORE` | 0x09 | u8 slot | `[v →]` pop into local |
| `LOAD_GLOBAL` | 0x0A | u16 slot | `[→ v]` push top-level variable |
| `STORE_GLOBAL` | 0x0B | u16 slot | `[v →]` pop into top-level variable |
| `ADD` | 0x10 | — | `[a b → a+b]` |
| `SUB` | 0x11 | — | `[a b → a-b]` |
| `MUL` | 0x12 | — | `[a b → a*b]` |
| `DIV` | 0x13 | — | `[a b → a/b]` always double (Dart `/`) |
| `IDIV` | 0x14 | — | `[a b → a~/b]` truncating int divide |
| `MOD` | 0x15 | — | `[a b → a%b]` Euclidean: result always in `[0, b.abs())` |
| `NEG` | 0x16 | — | `[a → -a]` |
| `BAND` | 0x17 | — | `[a b → a&b]` ints only |
| `BOR` | 0x18 | — | `[a b → a\|b]` ints only |
| `BXOR` | 0x19 | — | `[a b → a^b]` ints only |
| `SHL` | 0x1A | — | `[a b → a<<b]` shifts ≥ 64 give 0 |
| `SHR` | 0x1B | — | `[a b → a>>b]` arithmetic; ≥ 64 gives 0 or −1 |
| `BNOT` | 0x1C | — | `[a → ~a]` ints only |
| `EQ` | 0x20 | — | `[a b → bool]` |
| `NE` | 0x21 | — | `[a b → bool]` |
| `LT` | 0x22 | — | `[a b → bool]` |
| `LE` | 0x23 | — | `[a b → bool]` |
| `GT` | 0x24 | — | `[a b → bool]` |
| `GE` | 0x25 | — | `[a b → bool]` |
| `NOT` | 0x26 | — | `[a → !a]` |
| `JUMP` | 0x30 | i16 | pc += operand (relative to end of operand) |
| `JUMP_IF_FALSE` | 0x31 | i16 | `[v →]` jump when v is false |
| `JUMP_IF_FALSE_K` | 0x32 | i16 | peek, jump if false, **keep** v (for `&&`) |
| `JUMP_IF_TRUE_K` | 0x33 | i16 | peek, jump if true, **keep** v (for `\|\|`) |
| `CALL` | 0x40 | u16 fn, u8 argc | `[args… → result]` call moth function |
| `NATIVE` | 0x41 | u16 ref, u8 argc | `[args… → result]` call host native |
| `RET` | 0x42 | — | `[v →]` return top of stack |
| `RET_NULL` | 0x43 | — | return null |
| `TO_STRING` | 0x1D | — | `[v → text]` render as Dart's `print` would |
| `NEW_LIST` | 0x50 | u16 count | `[items… → list]` |
| `INDEX_GET` | 0x51 | — | `[list i → v]` bounds-checked |
| `INDEX_SET` | 0x52 | — | `[list i v → v]` bounds-checked |
| `NEW_INSTANCE` | 0x57 | u16 class | `[→ obj]` fields start null |
| `GET_PROP` | 0x58 | u16 name | `[obj → v]` field, or `.length` |
| `SET_PROP` | 0x59 | u16 name | `[obj v → v]` |
| `INVOKE` | 0x5A | u16 name, u8 argc | `[obj args… → result]` |
| `CLOSURE` | 0x5B | u16 fn, u8 captures_this | `[→ closure]` `this` captured when the flag is set |
| `CALL_VALUE` | 0x5C | u8 argc | `[callee args… → result]` call a function value |

`INVOKE` resolves the method on the receiver's class at run time, since the
compiler has no type information. Slot 0 of a method or constructor is the
receiver, so its arity is one more than its declared parameter count.

Arithmetic and comparison are numeric-only in M1a: `int op int → int`
(except `/`), any `double` operand promotes to double. Type errors are
runtime traps (`MOTH_ERR_TYPE`) carrying the function name and pc, not
undefined behavior.

`EQ`/`NE` accept any pair; differing types compare unequal, except int/double
which compare numerically.

## Truthiness

Dart is strict: only `true` is true. `JUMP_IF_FALSE` traps on a non-bool
condition rather than coercing — this catches the C-programmer reflex of
`while (1)` early and with a clear message.

## Calling convention

Caller pushes arguments left to right. `CALL` creates a frame whose slot *i*
is argument *i*; slots `arity..nlocals-1` start as `null`. `RET` pops the
frame and leaves exactly one value on the caller's stack. The value stack and
the frame stack are both fixed-size (`MOTH_STACK_MAX`, `MOTH_FRAMES_MAX`) —
overflow is a trap, never memory corruption. There is no recursion limit
beyond the frame stack.

## Natives

The blob's native table names each host function it needs (`gpioSet`,
`delayMs`, …) with its argc. At load time the VM resolves every entry against
the host's registration table; **an unresolved native fails the load** with
the offending name, rather than trapping later at the call site. This makes
"this blob needs a peripheral your board doesn't expose" a startup error.

A native receives `(vm, argc, argv, user)` and returns a value. It may
allocate through the vm handle — its arguments stay on the stack for the
duration of the call, so a collection cannot free them underneath it — but it
must not re-enter the interpreter.

## Trusting a blob

The VM validates what it can at load time: magic and version, every table's
bounds, `nlocals >= arity`, the entry and initializer indices, and — because
the class table is read before the function table — a second pass checking
that every method and constructor index is in range and has a receiver slot.
At run time, operand reads are bounds-checked against the function's code and
operand-driven pops are checked against stack depth.

Then every function is **verified** before anything runs: each is abstractly
interpreted along all reachable paths, tracking operand-stack depth. An
instruction reached twice must be reached at the same depth, which rejects
unbalanced jumps; the same walk checks for underflow and overflow, operand
indices, call arities, jumps that land outside the code or off an
instruction boundary, and unknown opcodes.

A one-off run of 500 byte-mutated blobs (not yet a committed harness):
457 are refused at load, 9 trap during the
run, 34 are harmless (mutations inside constant data), and none crash.

What verification still does not check is types — a program can put text
where a number belongs and trap at run time. That is a language-level
guarantee moth does not make yet, not a memory-safety hole.

## Not yet

Static members, exceptions, `async`. Closures, inheritance and
getters/setters have shipped and are part of the format above.

## Versioning

The version bumps on any change an older VM could not run correctly — a new
opcode, a changed operand layout, a new section. The loader compares exactly,
so a board running an older VM refuses a newer blob at load with a clear
message, rather than accepting it and trapping on an unknown opcode partway
through. That distinction matters most for hot push, where the blob and the
firmware are updated separately and can drift apart.

| version | added                                    |
|---------|------------------------------------------|
| 1       | integers, locals, control flow, calls    |
| 2       | top-level variables                      |
| 3       | heap, strings, lists, classes            |
| 4       | `OP_CLOSURE`, `OP_CALL_VALUE`            |
| 5       | `member_kind` byte per class method — a getter and a zero-argument method have identical arity, so the kind is explicit |
| 6       | assets section — compile-time decoded image pixels, 4-byte aligned to the blob start |
