# moth_render

Track-two native backend for moth (see [ADR-008](../docs/DECISIONS.md)):
scene graph + moth flex layout + software paint, implementing the
[backend contract](../docs/BACKEND.md). Desktop-first; no Dart, no VM, no
hardware required.

**Status: R0 scaffold.** Tree/props/events/animation and a first-pass flex
layout are implemented; paint is flat-color fills (no ThorVG yet, text renders
as a placeholder underline). See TODO(R1..R3) markers and the
[roadmap](../docs/ROADMAP.md#track-r--moth_render-parallel-best-effort).

## Build (macOS)

```
brew install sdl2            # for the harness window
cd moth_render
cmake -B build && cmake --build build
./build/harness
```

Without SDL2 the core library still builds — the contract API has zero
dependencies, which is what the ESP-IDF port and the conformance suite rely on.

## Layout

```
include/moth_render.h   the contract API (normative, versioned)
src/scene.cpp           tree ops, props, hit-testing, events, animation, frame
src/layout.cpp          moth flex subset (BACKEND.md §4)
src/paint.cpp           v0 software fills — ThorVG lands here in R2
harness/main.c          SDL2 dev loop driving the contract from plain C
esp/                    ESP-IDF port notes (R4, not started)
```
