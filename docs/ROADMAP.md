# Roadmap

Each milestone is independently useful and demoable. Order matters: nothing in
M3+ starts until M1's demo runs on real hardware.

## M0 — Design (done)

Architecture, decisions, and scope documented. This directory.

## M1 — The VM runs Dart on the board

**Demo: `fizzbuzz.dart` compiled on the Mac, output on the ESP32-P4 serial console.**

- [ ] Bytecode format + constant pool spec (docs/BYTECODE.md)
- [ ] `moth build`: analyzer front end → lowering → blob, for the M1 subset
      (functions, ints, strings, lists, control flow, closures)
- [ ] C interpreter: dispatch loop, call frames, arithmetic, comparisons
- [ ] Mark-sweep GC
- [ ] POSIX port + golden-file test harness (Dart source → expected stdout)
- [ ] ESP-IDF component port, `print()` → UART

## M2 — LVGL from Dart (imperative)

**Demo: buttons, sliders, and labels on the P4 touchscreen, driven from Dart, callbacks working.**

- [ ] Native-call mechanism + YAML binding spec + shim generator
- [ ] Bindings: obj create/delete, label, button, slider, image, style basics,
      flex layout, event registration
- [ ] Event queue: LVGL ISR/task → VM event loop
- [ ] Desktop simulator (`moth simulate`, SDL backend)

## M3 — Widgets and setState

**Demo: the README's VolumeScreen — declarative UI, reactive updates.**

- [ ] `package:moth`: Widget / State / Element, dirty list, build scheduler
- [ ] Reconciler: runtimeType+key matching, property diffing into lv_ calls
- [ ] Core widgets: Screen, Container, Row, Column, Label, Button, Slider,
      Image, Switch, ListView (non-virtualized)
- [ ] Animated widgets delegating to lv_anim
- [ ] Golden tests: widget tree in → sequence of LVGL calls out

## M4 — Hot push

**Demo: edit Dart, `moth run`, new UI on the device in ~2s over WiFi. No cable.**

- [ ] Device listener (mDNS + TCP), blob to spare partition, VM restart in place
- [ ] `moth run` = build + discover + push + attach log console
- [ ] Crash-loop protection (fall back to previous blob)

## M5 — v0.1 public release

- [ ] README demo GIF (simulator + real hardware)
- [ ] `moth create` project template
- [ ] Contributor docs; golden tests wired to CI (simulator, no hardware)
- [ ] Publish: GitHub + pub.dev for the CLI and `package:moth`

## Later / help wanted

Deliberately out of v0.x scope — meaty, self-contained problems for contributors:

- `async`/`await` lowering onto the event loop
- Stateful in-place hot reload (preserve State across pushes)
- Virtualized ListView backed by lv_table/lv_list recycling
- ESP32-S3 target; non-ESP ports (RP2350?) via the portable VM core
- Computed-goto dispatch + interpreter profiling on RISC-V
- InheritedWidget-style scoped state
- Debugger wire protocol (breakpoints over the hot-push channel)
