---
title: Decisions (ADRs)
sidebar_position: 12
slug: /decisions
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

## ADR-009: idiomatic Dart is the API; natives are a boundary, not a style

**Decision:** The user-facing device API is **idiomatic Dart** — classes,
named parameters, enums, getters and setters. Arduino's naming is used only
where it happens to also be good Dart, never as a design target. The flat
`digitalWrite(pin, value)` functions are the *native boundary*: minimal,
C-shaped, and eventually not what anyone writes.

```dart
// the boundary (generated, low-level, stable)   -> what the VM calls
digitalWrite(38, true);

// the API (package:moth, written in Dart)       -> what people write
final led = DigitalPin(38, mode: PinMode.output);
led.toggle();
```

**Why:** moth's entire premise is that the user already knows Dart — most of
them will never have written Arduino. Optimizing names for Arduino
familiarity serves the audience we are *not* targeting, and teaches Dart
developers a procedural style their own language abandoned. MicroPython made
the right call here (`machine.Pin` over a flat C surface) and it is the reason
its API reads like Python rather than like wrapped C.

**Why the flat functions exist today:** M1a has no classes, because classes
need the heap. They are provisional, and the docs say so rather than
presenting them as the destination.

**What stays from Arduino:** *capability* parity — the checklist of what a
beginner can build — and the pin/bus semantics themselves. Those are real
requirements. The spelling is not.

## ADR-008: moth_render is a parallel native backend; pixels stay native

**Status update (Aug 2026):** events overtook the caution below — every
shipped milestone (M2 through M5) runs on `moth_render`, and the LVGL
binding layer was never built. moth_render is the de facto primary backend;
the graduation review this ADR asks for should formalize that against the
conformance suite. The original decision and its reasoning stand unchanged
below.

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

## ADR-010: Network pushes are HMAC-paired; replay is accepted for v0.1

**Decision:** The WiFi push port authenticates with an HMAC-SHA256 frame
(`MPH2` in vm/host/push_proto.h): provisioning derives a 32-byte key from a
pairing phrase with **PBKDF2-HMAC-SHA256** (salt `moth-push-v1`, 600k
iterations — parameters live in vm/host/hmac_sha256.h and every deriver must
match), stores the key in NVS, and each push is signed over its nonce and
blob. A paired board rejects unsigned pushes at the header; a board whose
stored key exists but cannot be read **fails closed** — network push stays
off (serial still works) rather than silently reopening. Serial pushes are
never authenticated — physical possession of the cable is the pairing. An
unprovisioned board accepts unsigned pushes and warns at boot, because the
out-of-box path must not demand a secret before the first hello-world.

**Why an HMAC and not a bearer token:** a token crosses the network in the
clear on every push; one passive observation of one push and any machine on
the LAN can push arbitrary programs forever. With the HMAC the secret never
travels.

**Why a KDF and not a bare hash:** one captured frame is a complete offline
verifier for the phrase — an observer can grind candidate phrases against
`(nonce, blob, tag)` at hash speed, and a bare SHA-256 makes each guess cost
one hash with precomputation shared across every moth board. PBKDF2 at 600k
iterations multiplies the per-guess cost by ~10^6 and the fixed salt kills
cross-target precomputation; the sender pays the same cost once per push
(~2s in mothc; scripts cache the derived key in `MOTH_PUSH_KEY`). The
residual assumption, stated plainly: the phrase must carry real entropy. A
dictionary word still falls; the KDF buys orders of magnitude, not immunity.

**What is consciously deferred:** an observer can *replay* a signed frame it
captured — and because pushed programs persist to the `mothb` partition,
that is a permanent, attacker-timed rollback primitive: any previously
pushed program, reinstalled at a moment the attacker picks, surviving
reboot, with no freshness to expire it. Accepted for v0.1 because the
programs involved are ones the owner chose to run; closing it needs a
challenge-response round-trip (the receiver contributes freshness), which
v0.2 revisits deliberately. Also deferred: an unauthenticated peer can make
a paired board read (and for MPH2, allocate) up to MPSH_MAX_BLOB per
connection before rejection — a LAN attacker with reach can already flood
connections, the allocation fails gracefully under pressure, and the same
challenge-response closes both. TLS was rejected for the same reason mbedtls
isn't in the desktop build: the sim and the board must speak byte-identical
protocol from one small implementation.
