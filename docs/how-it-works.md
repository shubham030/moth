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
│ blink.dart                │              │  moth VM  (C, ~1500 lines) │
│    │                      │              │     │ interprets bytecode  │
│    ▼  mothc (Dart)        │   130 bytes  │     ▼                      │
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
program needs, and the bytecode for each function. Blink is 130 bytes.

Because a program is *data*, not a firmware image, updating it later means
sending a few hundred bytes rather than reflashing — that is what makes hot
push practical: `mothc app.dart --push` replaces the running program over the
USB cable or WiFi in well under a second. The full format is in
[BYTECODE](/docs/bytecode).

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
- **`vm/esp`** on the board wires the same names to real GPIO, ADC, LEDC PWM,
  I2C and UART.

The program cannot tell the difference. That is the point: develop and test on
your laptop, then run the identical bytes on hardware.

## The other half: rendering

moth's longer goal is Flutter's programming model — widgets and `setState` —
on these chips. That work is a separate track: a
[backend contract](/docs/backend) that a renderer implements, with
a working scene graph, flex layout and software rasterizer already running on
desktop and on two ESP32 boards.

The two halves have not met yet. Connecting them — Dart code driving pixels —
is the milestone that makes moth what it is meant to be. See the
[roadmap](/docs/roadmap).
