---
title: Architecture
sidebar_position: 9
slug: /architecture
---

# moth architecture

Four layers, each independently shippable and independently testable:

1. **Host toolchain** (`tools/mothc`, Dart) — compiles a Dart subset to a `.mothb` bytecode blob
2. **Device VM** (`vm/`, C) — interpreter + GC, portable across ESP-IDF and POSIX
3. **Renderer** (`moth_render/`, C++) — scene graph, flex layout, software paint, native animations
4. **Widget framework** (`packages/moth`, pure Dart) — compiled into the app blob like user code

This page describes the system as built. For a gentler walkthrough of the
same pipeline, start with [how it works](/docs/how-it-works).

## 1. Host toolchain (`tools/mothc`)

Runs on the developer machine. Pipeline:

```
app source ──► package:analyzer (AST) ──► lowering ──► moth bytecode ──► app.mothb
```

- **Front end: `package:analyzer`.** moth never implements a Dart parser; the
  official analyzer supplies syntax and error locations, so unsupported
  constructs are rejected with a source position and a hint rather than
  guessed at (ADR-004).
- **Lowering:** classes flatten to member tables with single inheritance;
  closures become heap objects capturing `this`; string interpolation becomes
  concatenation. What the subset excludes is rejected at compile time — see
  [language.md](/docs/language).
- **Output:** one self-contained blob — constant pool, native-import table,
  bytecode per function. Blink is 136 bytes; a full widget app is ~11KB.
  Format in [BYTECODE.md](/docs/bytecode).

Commands: `mothc app.dart` (compile), `mothc app.dart --push <target>`
(compile and hot-push over serial or WiFi), `mothc create <dir>` (scaffold).

## 2. Device VM (`vm/`)

A stack-based interpreter (ADR-003) in portable C11, ~1.7k lines of
implementation, no OS assumptions beyond malloc/tick/log shims. The same source compiles for
ESP-IDF and for macOS/Linux (the simulator and CI).

- **Values:** null, bool, 64-bit int, double, and heap objects (strings,
  lists, class instances, closures) — a tagged `moth_value` struct, not
  pointer tagging.
- **GC:** precise mark-sweep over the VM heap. No compaction and no
  generational nursery — measured first; neither has been needed yet.
- **Dispatch:** a classic `switch` inner loop. Interpretation speed is not
  the bottleneck: layout and painting are native, so bytecode runs only app
  logic and tree diffing (~4.5ms of a 26ms frame).
- **Verification:** every function is abstractly interpreted at load — stack
  effects checked, jumps bounded — so a malformed or hostile blob is refused
  before it runs. Types are checked dynamically at execution.
- **Concurrency:** single-threaded, one task. No isolates, no interrupts
  visible to Dart. There is no event loop yet; programs own their loop and
  call `pumpFrame` (async lowering is future work, see ROADMAP).
- **Natives:** the blob names the built-ins it needs; the VM resolves them by
  name at load and refuses to load if one is missing — "this board cannot
  run this program" is a load error, not a runtime surprise.

## 3. Renderer (`moth_render/`)

moth's own C++ scene graph behind the documented backend contract
([BACKEND.md](/docs/backend)): semantic nodes (box, label, slider, switch,
arc), moth-owned flex layout, an antialiased software rasterizer with
row-band damage tracking, native animations, and hit-testing with
finger-sized touch targets. The same code paints a desktop SDL window and
the board's panel; the panel itself sits behind a three-function interface
(`panel_init`, `panel_present_argb`, `panel_touch_read`), which is what
makes new boards ports rather than rewrites.

moth owns layout and style semantics (ADR-007), so a tree renders
identically everywhere — the same renderer code paints both hosts, and
contract tests plus per-frame paint budgets gate every commit (the fuller
conformance suite in [BACKEND.md](/docs/backend) §7 is planned). Measured on
an ESP32-S3 at 466x466: 38 fps ([ROADMAP](/docs/roadmap) has the
phase-by-phase tables).

The original design routed rendering through LVGL; the shipping renderer is
moth_render, and the LVGL binding layer was not built (ADR-008 records the
trade-offs). moth uses no LVGL code and never touches LVGL's XML format
(ADR-002).

## 4. Widget framework (`packages/moth`)

Pure Dart, compiled into the app blob like user code. Two trees: immutable
Widgets describing what should exist, and Elements holding state and node
ids (no RenderObjects — layout lives in the renderer, ADR-005). `setState`
marks an element dirty; the next `pumpFrame` rebuilds, and the reconciler
diffs against the scene graph — matching by type and key, updating nodes in
place, mounting and unmounting as children change.

Events flow the other way: the renderer hit-tests a touch, the VM's event
queue delivers it, and the framework bubbles it from the innermost element
to the first ancestor with a handler.

## Memory (measured on the verified board)

| Region | Actual |
| ------ | ------ |
| Framebuffer (466x466 ARGB) | ~850KB PSRAM |
| VM + renderer + bindings + fonts, flash | ~90KB on top of ESP-IDF (`idf.py size-components`: 48KB code + 42KB data) |
| Bytecode blob | ~11KB for a full widget app; stored programs execute from mapped flash, costing no RAM |
| VM heap | sized by the host; the S3 host gives the program its PSRAM remainder |

## The simulator (`mothsim`, `mothrun`)

`mothrun` is the headless host: virtual pins and buses against a virtual
clock, used by the golden tests. `mothsim` adds an SDL window, `--tap`, and
a push listener, so the full compile → push → verify loop runs with no
hardware. Everything the board hosts runs through the same code paths.
