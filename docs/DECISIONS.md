---
title: Decisions (ADRs)
nav_order: 13
permalink: /decisions/
---

# Decision records

Short ADRs. Each records a decision that shaped the architecture, so future
contributors (and future us) know what was considered and why.

## ADR-001: Custom bytecode VM, not a Dart VM port, not transpilation

**Decision:** Run Dart on-device via our own small bytecode VM.

**Rejected — porting the official Dart VM to ESP-IDF:** the SDK has RISC-V
support, but it's ~1M lines of C++ assuming a POSIX-ish OS; an MCU port means
maintaining a permanent fork against a fast-moving upstream that doesn't target
MCUs. (Google's own MCU attempt, Dartino, was abandoned in 2016.)

**Rejected — transpiling Dart to C or generating UI code:** codegen delivers
Dart *syntax* but not the reactive model; `setState`/rebuild semantics need a
live runtime. Codegen also can't do hot push.

**Accepted trade-off:** a Dart *subset* (see ARCHITECTURE), and we own a VM.
Precedent that this scale is solo-feasible: MicroPython, mruby, Berry.

## ADR-002: Target LVGL's C API only — never the LVGL XML format

**Decision:** All rendering goes through LVGL's MIT-licensed C API at runtime.
moth must not read, write, or generate LVGL XML.

**Why:** the LVGL XML *specification* is separately licensed
(https://lvgl.io/docs/pro/syntax/xml-license): distributing any third-party
tool or generator that processes it requires written permission from LVGL LLC,
explicitly "regardless of whether the software is commercial, free, or
open-source". An OSS project cannot build on it. The C API carries no such
restriction (SquareLine, GUI Guider, and others generate C calls commercially).

**Consequence:** declarative UI is achieved with our widget/reconciler layer
patching the retained lv_obj tree — which we prefer anyway (ADR-005).

## ADR-003: Stack-based bytecode, switch dispatch first

**Decision:** v1 is a stack machine with a plain `switch` dispatch loop.

**Why:** simplest correct thing; the compiler's expression lowering is trivial
against a stack machine. Register-based (Lua-style) is faster but complicates
the compiler, and interpreter throughput is not our bottleneck — LVGL does the
per-frame work in C; bytecode runs event handlers and diffs. Revisit with
profiles on real hardware, not before (computed-goto is a contained upgrade).

## ADR-004: `package:analyzer` as the compiler front end

**Decision:** The moth compiler consumes resolved ASTs from `package:analyzer`.

**Why:** never write a Dart parser; the analyzer is the official, pub-published,
stable-API front end with full resolution and const evaluation. The alternative
— consuming CFE kernel (.dill) files — is closer to what the real VM does and
may be a later swap, but its packages are not published/stable for external use.
The lowering layer is kept front-end-agnostic to preserve that option.

## ADR-005: Two trees, not three — LVGL is the render layer

**Decision:** The framework has Widgets (immutable, throwaway) and Elements
(retained, owning an `lv_obj` handle). No RenderObject layer.

**Why:** Flutter needs RenderObjects because it does its own layout and
painting. LVGL already provides retained objects, flex/grid layout, drawing,
and hit-testing — reimplementing that in interpreted Dart would be slower and
pointless. Reconciling widgets against LVGL's retained tree is exactly the
React-against-DOM architecture, which is well understood.

**Corollary (the performance rule):** state changes go through tree rebuilds;
continuous motion goes through LVGL's native `lv_anim` — the VM stays out of
the frame loop.

## ADR-006: MIT license; solo-first, OSS from day one

**Decision:** MIT, matching LVGL and embedded-ecosystem norms. Developed in the
open, but scoped selfishly: v0.x exists to serve the author's own hardware
(ESP32-P4 head unit) first. README states API instability plainly.

**Why:** copyleft would kill embedded adoption; solo-first keeps scope honest
until there's something worth contributing to. The contributor on-ramp is the
desktop simulator (no hardware required) plus golden-file tests that make small
PRs reviewable.

## ADR-007: moth owns layout/style semantics; backends are conformance-tested

**Decision:** Layout and style behavior is specified by moth (docs/BACKEND.md),
not inherited from any rendering engine. A backend is a conformance-tested
implementation of the contract; the contract speaks in semantic nodes (`label`,
`slider`) rather than drawing primitives.

**Why:** the original design said "map Row/Column to LVGL flex", which quietly
makes LVGL's flex quirks the de-facto spec and any second backend a
bug-for-bug chase. Owning the semantics makes backends independently buildable
against the layout-golden suite. Semantic-level nodes (not primitives) let the
LVGL backend keep using LVGL's mature native widgets while moth_render
composes its own.

**Cost accepted:** the LVGL backend must *compensate* wherever LVGL disagrees
with the spec, and we maintain a spec + conformance suite. Kept cheap by
scoping v1 layout to a small flex subset (no wrap/percent/margins).

## ADR-008: moth_render is a parallel native backend; pixels stay native

**Decision:** A second backend, `moth_render` (scene graph + moth flex layout +
ThorVG rasterization + damage tracking), is developed as **track two** —
desktop/SDL-first, best-effort, never blocking the LVGL-path milestones M1–M4.
ThorVG remains upstream C++ consumed as a component; no Dart port of any
rasterizer, ever: per-pixel work is 20–100× too slow interpreted. The split is
Flutter's own — engine native, framework Dart.

**Why build it at all:** full visual control beyond LVGL's ceiling, an
implementation-side pressure test of the backend contract (avoids designing an
abstraction with one consumer), and independence from LVGL LLC's
commercialization direction. **Why not primary:** partial-redraw maturity and
text quality are multi-year efforts LVGL already has; the P4 vendor
acceleration path (esp_lcd/PPA) is maintained for LVGL. moth_render graduates
to primary only if it passes conformance and beats LVGL where it matters on
hardware; if it stalls, nothing else is blocked.
