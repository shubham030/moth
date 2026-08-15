---
title: Built-ins
sidebar_position: 3
slug: /builtins
---

# Built-in functions

:::warning
These are the **low-level native boundary** — flat and C-shaped because that
is what a bytecode VM calls efficiently. `package:moth` already wraps them in
ordinary Dart — `OutputPin(38).toggle()` — and [hardware.md](hardware.md) is
the layer you should write against. See [ADR-009](DECISIONS.md).
:::

Every function below is available to any moth program without an import.

Built-ins are resolved **when the program loads**, not when they are called. If
your board doesn't provide one, you get a clear failure at startup —
*"this program needs 'i2cPing', which this board does not provide"* — instead
of a crash halfway through.

## Output

### `print(value)`
Prints a number, boolean, string, list or null, followed by a newline. On a
board this goes to the serial log.

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
PWM output, `duty` from 0 to 255 — hardware LEDC on ESP32, at 5 kHz. The
LEDC block gives moth six PWM channels, shared between `analogWrite` and
`tone`, allocated per pin on first use; a seventh pin is ignored with a
warning in the board log.

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

### `i2cReadBytes(addr, reg, n)` → `List<int>`
Reads `n` bytes from consecutive registers starting at `reg`, in a single
transaction — which is what you want for a sensor that spreads one reading
across several registers, because reading them one at a time can catch it
mid-update. **Returns an empty list if the device didn't answer**: every byte
in a result is a legal value, so there is no −1 to spare as a sentinel. `n` is
clamped to 1..64.

### `i2cWriteBytes(addr, reg, bytes)` → `bool`
Writes a `List<int>` to consecutive registers starting at `reg`, each item
truncated to a byte. False if the device didn't acknowledge.

## UART

### `uartBegin(port, tx, rx, baud)`
Opens a hardware serial port. Port 0 is usually the USB console — prefer 1 or 2.

### `uartWrite(port, byte)`
Sends one byte.

### `uartAvailable(port)` → `int`
How many bytes are waiting.

### `uartRead(port)` → `int`
Reads one byte, or −1 if nothing is waiting.

## Storage

Named integers that outlive the program: NVS flash on a board, memory in the
simulator. Keys are 1 to 15 characters — NVS's own limit, not one invented
here.

### `prefsGetInt(key, fallback)` → `int`
The value stored under `key`, or `fallback` when there is none. A key longer
than 15 characters could never have been stored, so it reads back as `fallback`
too rather than failing.

### `prefsSetInt(key, value)` → `bool`
Stores `value`. False if the key is invalid or the store is full — worth
checking on the writes you care about, because afterwards a save that failed
looks exactly like one that never happened.

## Servo

### `servoAttach(pin)`
Configures `pin` for 50Hz servo PWM. Call once before writing a pulse.

### `servoMicroseconds(pin, us)`
The pulse width that positions the horn. Clamped to 500..2500, so a value from
a knob can be fed in directly instead of grinding the servo against its stop.
Two servo channels exist on ESP32; a third `servoAttach` is ignored with a
warning in the board log.

A servo is never told an angle — it is told a pulse width and holds whatever
that means to it. `package:moth`'s `Servo(pin).write(degrees)` maps 0..180 onto
1000..2000us, which is a convention, not a measurement.

## Display

The display natives are the [backend contract](BACKEND.md) — the flat,
numeric boundary the widget layer in `package:moth` is built on. You will
normally never call them: write widgets (`Container`, `Text`, `Slider`, …)
and let `runApp`/`pumpFrame` drive this layer for you. They are listed here
because "every function below is available" should stay true.

| Function | What it does |
| --- | --- |
| `uiWidth()` / `uiHeight()` | display size in pixels |
| `uiRoot()` | the root node, owned by the backend |
| `uiCreate(kind)` | new node — 0 box, 1 label, 2 image, 3 slider, 4 switch, 5 arc |
| `uiDestroy(node)` | frees a node |
| `uiAttach(parent, child, index)` | inserts a child; index −1 appends |
| `uiDetach(node)` | removes a node from its parent |
| `uiSetNum(node, prop, value)` | sets a float property |
| `uiSetInt(node, prop, value)` | sets an integer property — colours, enums |
| `uiSetText(node, prop, text)` | sets a text property |
| `uiAnimate(node, prop, from, to, ms, easing)` | starts a native animation; returns its id, 0 on failure |
| `uiTick(dtMs)` | advances animations |
| `uiCommit()` | lays out, paints, presents; true when the frame repainted |
| `uiPoll()` | next event as `node * 8 + kind`, −1 when empty |
| `uiEventValue()` | the value carried by the last polled event |
| `uiFrameOf(node, which)` | a node's laid-out frame; `which` 0..3 = x, y, w, h |
| `uiSafeArea(which)` | the always-visible rectangle — the inscribed square on a round panel |

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
