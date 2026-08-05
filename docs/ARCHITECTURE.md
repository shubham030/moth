# moth architecture

Four layers, each independently shippable:

1. **Host toolchain** — Dart CLI that compiles a Dart package to a `.mothb` bytecode blob
2. **Device VM** — C interpreter + GC, packaged as an ESP-IDF component
3. **LVGL bindings** — native functions exposed to bytecode, generated from a binding spec
4. **Widget framework** — pure Dart (`package:moth`), compiled into the app blob like user code

## 1. Host toolchain (`moth` CLI)

Written in Dart, runs on the developer machine. Pipeline:

```
app source ──► package:analyzer (resolved AST) ──► lowering ──► moth bytecode ──► app.mothb
```

- **Front end: `package:analyzer`.** We never write a Dart parser; the official
  analyzer gives us resolved types, constant evaluation, and error reporting.
  (The kernel/.dill route via the CFE is a possible later swap — see ADR-004.)
- **Lowering:** desugar to a small core — classes flattened to vtables, closures
  to heap-allocated environments, generics erased, string interpolation to
  concat calls, `async` to state machines over the event loop (post-v1).
- **Output:** a single relocatable blob: constant pool, function table, bytecode,
  plus a name table for stack traces. Target size: a hello-world framework app
  well under 100KB.

Commands: `moth build`, `moth run` (build + push + attach console),
`moth simulate` (run against desktop LVGL — see §6).

## 2. Device VM

A stack-based bytecode interpreter (ADR-003) in portable C11, ~15–25k lines,
no OS assumptions beyond malloc/tick/log shims. ESP-IDF is the first port;
POSIX is the second (for the simulator and CI).

- **Object model:** everything is a `moth_obj*` — tagged small ints (31-bit
  smis), heap objects with a class-id header. Core types implemented natively:
  int, double, bool, String, List, Map, closure, instance.
- **GC:** precise mark-sweep over a dedicated PSRAM heap (default 4MB) with a
  small nursery for the widget-tree churn described in §5. No compaction in v1;
  allocation is free-list based. GC never runs inside an LVGL callback — the VM
  only allocates from the interpreter loop.
- **Dispatch:** classic `switch`-based inner loop first; computed-goto if the
  P4 profile demands it. Interpretation speed is *not* the bottleneck — LVGL
  does layout and rendering in C; bytecode only runs app logic and diffing.
- **Concurrency:** the VM is single-threaded and lives in one FreeRTOS task.
  LVGL events are queued to it; there is no Dart-visible threading (no isolates).
- **Event loop:** a run-to-completion queue (timers, input events, network).
  This is also the future foundation for `Future`/`async` lowering.

### Memory budget (ESP32-P4, 32MB PSRAM)

| Region              | Budget      |
| ------------------- | ----------- |
| LVGL frame buffers  | ~1.5MB (2 × 480×320×16bpp double-buffered, adjust per panel) |
| Dart heap           | 4MB default, configurable |
| Bytecode blob       | < 512KB, executed in place from PSRAM |
| VM C footprint      | < 200KB flash, < 64KB internal SRAM (interpreter stack, GC roots) |

## 3. LVGL bindings

Bytecode calls into C through a native-function table: `NATIVE_CALL idx` pops
args, invokes a registered C shim, pushes the result. Shims are generated from
a YAML binding spec (name, LVGL function, arg marshalling) — adding a widget
binding is a spec entry, not hand-written C.

- LVGL objects surface in Dart as opaque handles owned by the element tree
  (§5); user code normally never touches them directly.
- Callbacks: LVGL events carry a closure id; the C event handler enqueues
  `(closure id, payload)` onto the VM event loop. Native code never re-enters
  the interpreter.
- Only the MIT-licensed LVGL C API is used — never the LVGL XML spec (ADR-002).

## 4. Widget framework (`package:moth`)

Flutter's model minus the render layer. Two trees, not three:

- **Widgets** — immutable, throwaway descriptions built by `build()`.
- **Elements** — retained; each holds its widget, its `State` (if stateful),
  and the `lv_obj` handle it manages. There are no RenderObjects: LVGL *is*
  the render tree, and flex/grid layout, painting, and hit-testing are its job.

Reconciliation is the React/Flutter algorithm: `setState` marks the element
dirty; on the next event-loop tick, dirty subtrees re-run `build()`, children
are matched by `runtimeType` + `key`, and mismatches create/destroy `lv_obj`s
while matches diff their properties into `lv_*_set_*` calls.

### Performance model — the one rule

**State changes go through the tree; motion does not.**

- Interaction-driven rebuilds allocate tens of small widget objects and walk a
  short diff — low milliseconds even interpreted at 400MHz. That is the entire
  cost of `setState`, and it is paid on taps, not per frame.
- Continuous animation never rebuilds. Animated widgets (`AnimatedOpacity`-style)
  configure LVGL's native `lv_anim` engine, which runs at frame rate in C. The
  VM is not in the frame loop.

## 5. Hot push

The app is data. A device-side listener (TCP, mDNS-advertised) accepts a new
`.mothb` blob, writes it to a spare flash partition, and restarts the VM with
the new blob — LVGL and WiFi stay up, so the cycle is edit → `moth run` →
new UI in ~2 seconds, no reflash, no cable. (In-place stateful hot *reload* is
a research topic, not v1 — see ROADMAP.)

## 6. Desktop simulator

The same VM compiled for POSIX + LVGL's SDL backend. `moth simulate` opens the
app in a desktop window — this is the contributor on-ramp (no hardware needed),
the CI target, and the fast inner loop for framework development.

## Dart subset

v1 supports: classes, single inheritance, interfaces/implicit interfaces,
closures, `int`/`double`/`bool`/`String`/`List`/`Map`, named & optional params,
string interpolation, exceptions (`try`/`catch`/`finally`), top-level and
static members, `const` where the compiler can fold it.

Deferred: `async`/`await` (event-loop lowering, post-v1), mixins, extension
methods, records/patterns. Out of scope: isolates, mirrors, FFI-from-Dart,
finalizers, most of `dart:*` beyond a curated `dart:core` slice.

The subset is checked at compile time — unsupported constructs are hard errors
with clear messages, never silent misbehavior.
