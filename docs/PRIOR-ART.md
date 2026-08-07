---
title: Prior art
sidebar_position: 8
slug: /prior-art
---

# Prior art

Surveyed August 2026. Two questions matter: does a Dart runtime for
microcontrollers exist, and does a Flutter-style reactive widget framework for
microcontrollers exist. **The answer to both is no** — but capable projects own
each half separately, and they are worth learning from rather than ignoring.

## Dart on microcontrollers: nothing exists, durably

- **Dartino** (https://github.com/dart-archive/sdk) — Google's Dart VM for
  embedded, ran on STM32F746 and FRDM-K64F. **Archived since January 2020.**
- **The Dart team has declined the space on the record.** On
  dart-lang/sdk#52105, a Dart VM engineer: *"We have no current plans of
  porting and supporting Dart on ESP32. We experimented with this space
  [Dartino], but discontinued our efforts to avoid spreading ourselves too
  thin."* dart-lang/sdk#44099 ("compile Dart to microcontroller") — *"Short
  answer is no."*
- **dart_eval** (https://github.com/ethanblake4/dart_eval) — bytecode compiler
  + interpreter for Dart, *written in Dart*, `package:analyzer` front end (same
  choice as moth). Cannot run on an MCU: the interpreter needs the Dart runtime
  underneath it. Useful as a bytecode-design reference, and as a performance
  anchor — its author measures it at 10–50× slower than AOT Dart, "on par with
  Ruby."
- **dart2wasm** targets WasmGC, which wasm3/WAMR on ESP32 do not implement.
  Closed route today. No Dart→C transpiler exists.

Consequence: the runway is clear, and moth carries the entire cost of Dart
semantics alone. Nobody upstream will help, and nobody upstream will compete.

## Flutter on embedded Linux: the floor moth undercuts

flutter-pi (MIT), sony/flutter-embedded-linux (BSD-3), toyota/ivi-homescreen —
all active, all requiring an MMU, a full Linux userspace, a GPES2+ GPU, and
hundreds of MB of RAM. Flutter's engine has no practical software-render path
for embedded, so a GPU is effectively mandatory. The gap between the smallest
Flutter (Pi Zero 2 W: quad A53, 512MB, GPU, Linux) and moth's target (ESP32,
no MMU, no GPU) is roughly **three orders of magnitude of RAM**.

## Declarative UI on MCUs: exists, but not Flutter's model

- **Slint** (https://slint.dev) — the closest thing to "Flutter for MCUs."
  `.slint` DSL **AOT-compiled** to Rust/C++, reactive *property bindings*,
  `no_std` runtime, officially supports ESP32-S3/C3. Renders **one line at a
  time into two line buffers with dirty-region tracking**, demoed on an RP2040
  with 264KB RAM.
  - Differs from moth: compile-time, not a VM — no code push, no hot reload;
    property bindings rather than widget-subtree rebuild + reconciliation; and
    it is a new DSL, not the language you already write your app in.
  - **Licensing:** GPLv3, or a royalty-free license that *excludes embedded*,
    or commercial from ~$1/device. Reinforces moth's MIT positioning.
  - **Directly instructive:** their line-buffer + dirty-region approach is the
    known-good answer to our R3 problem, and proof the layout+paint half fits
    in 264KB.
- **Qt Quick Ultralite** — QML subset, genuinely declarative, Cortex-M4+,
  **commercial license only**.
- **LVGL v9** — has an XML UI description plus a Subject/Observer binding
  system, so it *is* reactive at the property level. Still C, retained-mode,
  and the XML spec's license restricts third-party tooling (see ADR-002).
- **Moddable SDK / Piu** (JS) — the closest existing "high-level language VM +
  real UI framework on an MCU." Declarative templates but **imperative
  updates** inside behavior callbacks, not reactive rebuild.

No project was found implementing Flutter's actual model — immutable widget
tree, `setState`, Element reconciliation — on an MCU, in any language.

## The closest competitor: Toit

https://github.com/toitlang/toit — LGPL-2.1 VM/compiler, MIT stdlib. Very
active (v2.0.0-alpha, releases roughly monthly), and **added ESP32-P4 support
in May 2026** — moth's exact target silicon.

**Read the founders carefully: Kasper Lund and Erik Corry, from V8 and Dart.**
The people who built Dart concluded that the right move for microcontrollers
was *a new language*, not a Dart port. That is the strongest available
counter-argument to this project's premise, and it deserves a straight answer
rather than a dismissal.

The answer moth gives: **Toit's own language is the cost moth refuses to pay.**
Toit asks an app developer to learn Python-ish syntax, a new stdlib and a new
mental model to make an LED blink. moth's entire premise is that the developer
already knows Dart — the reuse *is* the product. Toit is also IoT/networking
first: its UI story (`toit-pixel-display`, 3 stars, e-paper oriented) is
essentially absent, with no flex layout, touch widgets, or reactive rebuild.

What to steal from Toit, openly: **Jaguar** pushes new code over local WiFi with
~2-second live reload, and **Artemis** does fleet OTA with binary diffs. That is
a five-year head start on the deployment story our M4 implies, and the bar to
measure against.

## "One language everywhere" — the positioning check

- **Embedded Swift** is the strongest rival pitch: the language you write iOS
  apps in, running on ESP32-C6, with Apple/Espressif partnership. But
  **SwiftUI does not run there** (Espressif's own graphics demo bolts on SDL3),
  and it **cannot target Xtensa** — so ESP32/S2/S3 are out entirely.
- **Toit** reuses no app-side skills. **MicroPython/TinyGo** share a language
  with your backend but offer no UI framework.

Nobody offers the same language *and* the same UI programming model across
phone and microcontroller. That is moth's unclaimed position — and the reason
the beginner framing ("learn Dart once, use it on both") is the honest pitch
rather than a marketing angle.
