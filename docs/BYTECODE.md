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
version      u16       MOTH_BYTECODE_VERSION (currently 2)
flags        u16       reserved, 0
constants    u16 count, then each: tag u8 + payload
natives      u16 count, then each: name_const u16 + argc u8
globals      u16       number of top-level variable slots
functions    u16 count, then each:
               name_const  u16
               arity       u8
               nlocals     u8    total slots (params occupy slots 0..arity-1)
               code_len    u32
               code        code_len bytes
entry        u16       index of the function to call at start (must be arity 0)
init         u16       initializer function index, or 0xFFFF for none
```

Global slots start as `null` and are filled by the `init` function, which the
VM runs to completion before `entry`. The compiler synthesizes it from the
top-level variable initializers, in declaration order.

Constant tags: `0 = int` (i64), `1 = double` (f64), `2 = string`
(u16 byte length + UTF-8, no terminator), `3 = bool` (u8), `4 = null`.

Strings appear in the pool for names even when the VM has no string *values*
yet (M1a) — they are used for native resolution and diagnostics.

## Values (M1a)

`null`, `bool`, `int` (64-bit signed), `double`. Heap objects, strings as
values, and GC arrive in M1b; opcodes are numbered leaving room for them.

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

A native receives `(argc, argv)` and returns a value. Natives must not
re-enter the VM in M1a.

## Not in M1a

Heap objects, strings as values, closures, classes, GC, exceptions, `async`.
Opcode space is left free at 0x50+ for them. The format version bumps on any
change that invalidates existing blobs.
