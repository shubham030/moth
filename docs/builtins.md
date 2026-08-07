---
title: Built-ins
sidebar_position: 3
slug: /builtins
---

# Built-in functions

{: .warning }
These are the **low-level native boundary**, not the API you are meant to write
long-term. They are flat functions because M1a has no classes yet. Once classes
land, `package:moth` wraps all of this in idiomatic Dart —
`DigitalPin(38, mode: PinMode.output).toggle()` — and these become an
implementation detail. See [ADR-009](/docs/decisions).

Every function below is available to any moth program without an import.

Built-ins are resolved **when the program loads**, not when they are called. If
your board doesn't provide one, you get a clear failure at startup —
*"this program needs 'i2cPing', which this board does not provide"* — instead
of a crash halfway through.

## Output

### `print(value)`
Prints a number, boolean or null, followed by a newline. On a board this goes
to the serial log. Strings arrive in M1b.

## Timing

### `delay(ms)`
Waits for `ms` milliseconds. In the simulator this advances the virtual clock
instantly unless you pass `--real-time`.

### `delayMicroseconds(us)`
Busy-waits for `us` microseconds.

### `millis()` → `int`
Milliseconds since the program started.

### `micros()` → `int`
Microseconds since the program started.

## Digital I/O

### `pinOutput(pin)`
Configures `pin` as an output. Required before `digitalWrite`.

### `pinInput(pin)` / `pinInputPullup(pin)`
Configures `pin` as an input, optionally with the internal pull-up resistor
enabled. A pulled-up pin reads `true` when nothing is pulling it down — which
is why buttons usually read `false` when pressed.

### `digitalWrite(pin, value)`
Sets an output pin. `value` is a real `bool`, not `1`/`0`.

### `digitalRead(pin)` → `bool`
Reads an input pin.

## Analog I/O

### `analogRead(pin)` → `int`
Reads the ADC on `pin`. Returns −1 if that pin has no ADC. On ESP32 this is
ADC1 one-shot at 12 dB attenuation.

### `analogWrite(pin, duty)`
PWM output, `duty` from 0 to 255 — hardware LEDC on ESP32, at 5 kHz.

### `tone(pin, hz)` / `noTone(pin)`
Square wave at `hz` on `pin`, and off again.

## Random

### `randomSeed(n)` / `random(max)` → `int`
Returns a value in `0..max-1`. The generator is deterministic for a given
seed, so tests can assert on the sequence.

## I2C

### `i2cBegin(sda, scl)`
Starts the I2C bus on those pins. Call once before the others.

### `i2cPing(addr)` → `bool`
True if a device acknowledges at that 7-bit address. This is what an I2C
scanner is built from.

### `i2cWriteReg(addr, reg, value)` → `bool`
Writes one byte to one register. False if the device didn't acknowledge.

### `i2cReadReg(addr, reg)` → `int`
Reads one byte from one register. **Returns −1 if the device didn't answer** —
check for it rather than assuming success.

Multi-byte transfers need lists, so they arrive in M1b.

## UART

### `uartBegin(port, tx, rx, baud)`
Opens a hardware serial port. Port 0 is usually the USB console — prefer 1 or 2.

### `uartWrite(port, byte)`
Sends one byte.

### `uartAvailable(port)` → `int`
How many bytes are waiting.

### `uartRead(port)` → `int`
Reads one byte, or −1 if nothing is waiting.

## Writing your own helpers

Anything not listed here you can often just write in Dart. Arduino's `map` and
`constrain` are macros in C; here they are ordinary functions:

```dart
int constrain(int v, int low, int high) {
  if (v < low) return low;
  if (v > high) return high;
  return v;
}
```
