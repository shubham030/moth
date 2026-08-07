---
permalink: /
title: Home
nav_order: 1
---

# Write Dart. Run it on a microcontroller.

moth lets you program an ESP32 in the same language you write Flutter apps in.
Not a lookalike syntax, not a code generator — a real interpreter running your
compiled Dart on the chip.

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

```console
$ mothc examples/blink.dart
wrote examples/blink.mothb (130 bytes)

$ mothrun examples/blink.mothb --stop-after 2000
[     0ms] pin 38 -> output
[     0ms] pin 38 = HIGH
[   500ms] pin 38 = low
[  1000ms] pin 38 = HIGH
[  1500ms] pin 38 = low
```

That ran with no hardware attached. The same 130 bytes run unchanged on the
board.

## Why this exists

If you are learning Dart for Flutter, the microcontroller on your desk should
not require a second language, a second toolchain and a second mental model.
One language for the app and the device is the whole idea.

There is a real gap here: no Dart runtime for microcontrollers existed before
this one — Google tried, archived it in 2020, and
[said on the record]({{ site.baseurl }}/prior-art/) they will not resume.

## Start here

- **[Getting started]({{ site.baseurl }}/getting-started/)** — install, build, blink, flash
- **[Arduino parity]({{ site.baseurl }}/arduino-parity/)** — what works today, with tests
- **[Built-ins]({{ site.baseurl }}/builtins/)** — the full API reference
- **[Language]({{ site.baseurl }}/language/)** — which parts of Dart run today
- **[Testing]({{ site.baseurl }}/testing/)** — run the suite, add a case

## Honest status

Early. Dart runs on hardware, digital and analog I/O work, I2C and UART work,
and every claim on the [parity page]({{ site.baseurl }}/arduino-parity/) has a test behind it.

What does not exist yet: strings, lists, classes and garbage collection (all
land together in M1b), and the Flutter-style widget layer that gives the
project its name. See the [roadmap]({{ site.baseurl }}/roadmap/).
