# moth

**Flutter's programming model on a $6 microcontroller.**

moth lets you write UI and application logic for ESP32-class microcontrollers in
real Dart — `StatefulWidget`, `setState`, `build()` — and run it on-device via a
small bytecode VM, rendering through [LVGL](https://lvgl.io).

```dart
class VolumeScreen extends StatefulWidget {
  State createState() => _VolumeState();
}

class _VolumeState extends State<VolumeScreen> {
  int volume = 40;

  Widget build() => Column(children: [
        Label('Volume: $volume'),
        Slider(value: volume, onChanged: (v) => setState(() => volume = v)),
      ]);
}
```

> **Status: early, but real.** Dart already runs on hardware — the VM executes
> compiled Dart on an ESP32, driving GPIO. The widget layer above is next.
> See [ROADMAP](docs/ROADMAP.md).

## Learn one language, use it everywhere

If you are learning Dart to build Flutter apps, moth means the microcontroller
on your desk speaks the same language. No second syntax, no toolchain detour —
the `for` loop you already know, on a chip that costs a few dollars.

```dart
void main() {
  var led = 38;
  pinOutput(led);

  var on = false;
  while (true) {
    on = !on;
    digitalWrite(led, on);
    delay(500);
  }
}
```

```
$ dart run tools/mothc/bin/mothc.dart examples/blink.dart
wrote examples/blink.mothb (130 bytes)

$ mothrun examples/blink.mothb --stop-after 2000    # no hardware needed
[     0ms] pin 38 -> output
[     0ms] pin 38 = HIGH
[   500ms] pin 38 = low
[  1000ms] pin 38 = HIGH
[  1500ms] pin 38 = low
```

The same blob runs unchanged on the board. And because a program is 130 bytes
of bytecode rather than a firmware image, updating it later means pushing those
bytes — not reflashing.

## Why

Flutter itself cannot run on a microcontroller: it needs an MMU, an OS, hundreds
of MB of RAM, and a GPU. But the part of Flutter developers love — declarative
widgets, reactive state, hot reload — is a *programming model*, not an engine.
moth reimplements that model at MCU scale:

- **Host toolchain (Dart):** compiles your Dart source to a compact custom
  bytecode, reusing the official Dart analyzer as its front end.
- **Device VM (C, ESP-IDF component):** a small bytecode interpreter with GC,
  in the spirit of MicroPython / mruby. No Dart VM port, no Linux.
- **Widget framework (pure Dart, runs on the VM):** widgets diff against LVGL's
  retained object tree the way React diffs against the DOM. LVGL does layout,
  drawing, and input; moth does state and reconciliation.
- **Hot push:** your app is a bytecode blob. Push a new one over WiFi and the
  UI restarts in place — no reflash, no cable.

```
┌─ your Mac ──────────────────────┐      ┌─ ESP32 ─────────────────────┐
│  app.dart                       │      │  moth VM (C, ~20k lines)    │
│    │  moth compile              │ WiFi │    │ interprets bytecode    │
│    ▼                            │ ───► │    ▼                        │
│  app.mothb  (bytecode blob)     │ /USB │  widget framework (Dart)    │
└─────────────────────────────────┘      │    │ diff & patch           │
                                         │    ▼                        │
                                         │  LVGL 9 (layout + render)   │
                                         └─────────────────────────────┘
```

## What moth is not

- Not Flutter. No Impeller/Skia, no RenderObjects, no `dart:ui`. Rendering is
  LVGL; widgets are moth's own (deliberately Flutter-flavored) API.
- Not full Dart. The VM runs a practical subset — see
  [ARCHITECTURE](docs/ARCHITECTURE.md#dart-subset) for exactly what.
- Not an LVGL XML tool. moth calls LVGL's MIT-licensed C API at runtime and
  does not read or write the separately-licensed LVGL XML format
  ([ADR-002](docs/DECISIONS.md#adr-002)).

## Hardware targets

Primary: **ESP32-P4** (dual-core RISC-V @ 400MHz, up to 32MB PSRAM). The VM is
plain C on ESP-IDF, so ESP32-S3 and other IDF targets should follow with little
work. The framework needs a few MB of PSRAM; bare 512KB-SRAM chips are out of
scope for now.

## Docs

- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — VM, bytecode, GC, bindings, widget layer
- [BYTECODE.md](docs/BYTECODE.md) — instruction set and blob format
- [BACKEND.md](docs/BACKEND.md) — the rendering-backend contract (nodes, layout, events)
- [PRIOR-ART.md](docs/PRIOR-ART.md) — what exists already, and why this is still worth building
- [ROADMAP.md](docs/ROADMAP.md) — milestones and help-wanted
- [DECISIONS.md](docs/DECISIONS.md) — why it's built this way (ADRs)

## License

MIT. moth is maintained first for the author's own hardware projects; the API
is unstable until v0.1 ships.
