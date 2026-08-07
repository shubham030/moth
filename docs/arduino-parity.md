---
title: Arduino parity
sidebar_position: 4
slug: /arduino-parity
---

# Hardware capabilities

The v1 goal is simple: **anything an Arduino tutorial teaches, you can build in
Dart.** This page tracks that claim honestly — every ✅ has an executable test
behind it, and every ❌ says what it is waiting on.

The Arduino column is a translation aid, not a design target. moth copies
Arduino's *capabilities*, not its spelling — see
[the API this is heading toward](#the-api-this-is-heading-toward) below, and
[ADR-009](/docs/decisions) for why.

## Digital I/O — done

| Arduino | moth | Test |
| --- | --- | --- |
| `pinMode(p, OUTPUT)` | `pinOutput(p)` | `blink`, `peripherals` |
| `pinMode(p, INPUT)` | `pinInput(p)` | `peripherals` |
| `pinMode(p, INPUT_PULLUP)` | `pinInputPullup(p)` | `button` example |
| `digitalWrite(p, HIGH/LOW)` | `digitalWrite(p, true/false)` | `blink` |
| `digitalRead(p)` | `digitalRead(p)` → `bool` | `button` example |

Arduino's `HIGH`/`LOW` become real booleans, which is both clearer and lets the
compiler catch mistakes: `digitalWrite(led, 1)` is an error, not a surprise.

## Timing — done

| Arduino | moth | Test |
| --- | --- | --- |
| `delay(ms)` | `delay(ms)` | `blink`, `peripherals` |
| `delayMicroseconds(us)` | `delayMicroseconds(us)` | `peripherals` |
| `millis()` | `millis()` | `peripherals` |
| `micros()` | `micros()` | `peripherals` |

## Analog I/O — done

| Arduino | moth | Test |
| --- | --- | --- |
| `analogRead(p)` | `analogRead(p)` → `int` | `peripherals` |
| `analogWrite(p, 0..255)` | `analogWrite(p, 0..255)` | `peripherals` |
| `tone(p, hz)` | `tone(p, hz)` | `peripherals` |
| `noTone(p)` | `noTone(p)` | `peripherals` |

On ESP32 `analogWrite` and `tone` are backed by LEDC hardware PWM, and
`analogRead` by ADC1 one-shot.

## Buses — done

| Arduino | moth | Test |
| --- | --- | --- |
| `Wire.begin(sda, scl)` | `i2cBegin(sda, scl)` | `peripherals`, `i2c_scan` |
| device probe | `i2cPing(addr)` → `bool` | `i2c_scan` on hardware |
| register write | `i2cWriteReg(addr, reg, value)` → `bool` | `peripherals` |
| register read | `i2cReadReg(addr, reg)` → `int` (−1 = no answer) | `peripherals` |
| `Serial1.begin(baud)` | `uartBegin(port, tx, rx, baud)` | `peripherals` |
| `Serial1.write(b)` | `uartWrite(port, byte)` | `peripherals` |
| `Serial1.available()` | `uartAvailable(port)` → `int` | `peripherals` |
| `Serial1.read()` | `uartRead(port)` → `int` (−1 = empty) | `peripherals` |

Single-register I2C access covers the majority of sensors and needs no heap,
which is why it lands in v1. Multi-byte transfers arrive with lists.

Verified on hardware: `examples/i2c_scan.dart` finds all eight devices on a
Waveshare ESP32-S3-Touch-AMOLED board (0x18, 0x20, 0x34, 0x40, 0x50, 0x51,
0x5A, 0x6B).

## Randomness and math — done

| Arduino | moth | Note |
| --- | --- | --- |
| `random(max)` | `random(max)` | deterministic per seed, so tests can assert on it |
| `randomSeed(n)` | `randomSeed(n)` | |
| `min/max/abs/constrain/map` | write them in Dart | see below |

Arduino ships these as macros because C makes them awkward. In moth they are
ordinary Dart you can read and change:

```dart
int constrain(int v, int low, int high) {
  if (v < low) return low;
  if (v > high) return high;
  return v;
}

int map(int v, int inMin, int inMax, int outMin, int outMax) {
  return (v - inMin) * (outMax - outMin) ~/ (inMax - inMin) + outMin;
}
```

## Not yet — and what each is waiting on

| Arduino | moth status | Blocked on |
| --- | --- | --- |
| `Serial.print("text")` | ❌ numbers only | strings (M1b) |
| `String`, `char` | ❌ | heap + GC (M1b) |
| arrays, `Wire.requestFrom(n)` | ❌ | lists (M1b) |
| `attachInterrupt(pin, fn, mode)` | ❌ | closures + event loop (M1b/M2) |
| `Servo`, `EEPROM`, `SPI` | ❌ | lists, then library work |
| `struct`, custom types | ❌ | classes (M1b) |

Everything in that table depends on the same milestone: **M1b adds the heap**,
which unlocks strings, lists and classes together. Nothing there needs new VM
architecture — it needs the allocator and collector.

## The API this is heading toward

Everything above is the **native boundary** — deliberately flat and C-shaped,
because that is what a bytecode VM calls efficiently. It is not meant to be
what you write.

Once classes land in M1b, `package:moth` wraps that boundary in ordinary Dart:

```dart
final led = DigitalPin(38, mode: PinMode.output);
led.toggle();

final knob = AnalogPin(4);
print(knob.read());

final buzzer = PwmPin(6);
buzzer.tone(440);

final bus = I2c(sda: 15, scl: 14);
for (final addr in bus.scan()) {
  print('found 0x${addr.toRadixString(16)}');
}
bus.device(0x5a).writeRegister(0x01, 200);
```

Named parameters, enums instead of magic booleans, real objects you can pass
around and test. The same reason MicroPython gives you `machine.Pin` instead of
a pile of loose functions: the low-level surface is an implementation detail,
not the language you should have to think in.

The flat functions will keep working underneath — but the tutorials, examples
and this page will all be rewritten around the class API when it exists.

## What moth already does that Arduino cannot

- **Run your program with no hardware.** `mothrun` simulates pins, buses and a
  virtual clock, so a blink program completes instantly instead of in real time.
- **Update without reflashing.** A program is a few hundred bytes of bytecode,
  not a firmware image.
- **Test peripherals in CI.** Fake analog values and fake I2C devices are
  command-line flags, so hardware behavior is a golden test.
- **Catch mistakes in the language.** `digitalWrite(pin, 1)` and a missing
  variable are compile errors with a source location and a hint.
