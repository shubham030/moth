---
title: How it works
sidebar_position: 6
slug: /how-it-works
---

# How it works

Four pieces. Your Dart never runs on your Mac — it is compiled there and
*interpreted* on the chip.

```
your Mac                                   the microcontroller
┌───────────────────────────┐              ┌────────────────────────────┐
│ blink.dart                │              │  moth VM  (C, ~1700 lines) │
│    │                      │              │     │ interprets bytecode  │
│    ▼  mothc (Dart)        │   138 bytes  │     ▼                      │
│  parse → lower → emit     │ ───────────► │  natives: gpio, i2c, uart  │
│    │                      │   .mothb     │     │                      │
│    ▼                      │              │     ▼                      │
│  blink.mothb              │              │  the actual pins           │
└───────────────────────────┘              └────────────────────────────┘
```

## 1. The compiler (`tools/mothc`)

Written in Dart, using the official analyzer's parser — so moth never
reimplements Dart's syntax, and stays honest about what the language means.

It walks the syntax tree and emits bytecode for a stack machine: `LOAD` a
local, `ADD` two values, `JUMP_IF_FALSE` over a branch. A `while` loop becomes
a conditional jump backwards. Nothing exotic; the design is deliberately the
boring, well-understood one.

Unsupported syntax is rejected at compile time with a source location, rather
than failing mysteriously on the device.

## 2. The blob (`.mothb`)

A small self-contained file: a constant pool, a table of the built-ins the
program needs, and the bytecode for each function. Blink is 138 bytes.

Because a program is *data*, not a firmware image, updating it later means
sending a few hundred bytes rather than reflashing — that is what makes hot
push practical: `mothc app.dart --push` replaces the running program over the
USB cable in well under a second, or over paired WiFi (the pairing key
derivation adds about two seconds). The full format is in
[BYTECODE](BYTECODE.md).

## 3. The VM (`vm/`)

Portable C11 with no OS assumptions — the same source compiles for macOS and
for ESP-IDF. It is a switch-dispatch interpreter over a value stack and a
frame stack, both fixed-size, so overflow is a clean error rather than memory
corruption.

Two design choices worth calling out:

**Built-ins are resolved at load time.** The blob names what it needs
(`i2cPing`, `analogRead`); the VM matches those against what the host
registered and refuses to load if something is missing. "This board doesn't
have that" is a startup message, not a mid-run crash.

**Operand reads are bounds-checked.** A corrupt or hostile blob produces an
error, not an over-read — which matters once programs arrive over the network.

## 4. The host

The layer that supplies the built-ins. There are two:

- **`mothrun`** on the desktop simulates everything against a virtual clock, so
  `delay(500)` costs nothing and a blink program finishes instantly. Fake analog
  values and fake I2C devices are command-line flags, which is what makes
  peripheral behavior testable in CI.
- **`vm/esp`** on the board wires the same names to real GPIO, ADC, LEDC
  PWM, I2C, UART, servos, tone and NVS-backed preferences. Both ESP hosts
  compile the same `hw_natives.c`, so the headless and display firmwares
  cannot drift apart.

The program cannot tell the difference. That is the point: develop and test on
your laptop, then run the identical bytes on hardware.

## The other half: rendering

The same VM also drives a display. moth's renderer (`moth_render`) implements
a documented [backend contract](BACKEND.md) — a scene graph with flex
layout, an antialiased software rasterizer with damage tracking, native
animations and touch hit-testing. On top of it, `package:moth` provides
Flutter-shaped widgets: `Component`, `build()`, `setState`, and the names you
expect (`Container`, `Column`, `Text`, `Slider`, `Switch`).

A tap rebuilds the widget tree, the reconciler patches only the changed
nodes, and the renderer repaints only the changed rows — 38 fps on a
466x466 panel, measured. The same app runs unchanged in the desktop
simulator (`mothsim`) and on the board. See the [roadmap](ROADMAP.md) for
what is built and what is next.
