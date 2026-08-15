---
title: Hardware
slug: /hardware
---

# Hardware

```dart
import 'package:moth/hardware.dart';

final led = OutputPin(38);
led.value = true;
```

Every class here wraps the flat built-ins in [builtins.md](builtins.md). Those
still work, and always will — they are the boundary the VM actually speaks. But
they are not what you should be writing.

## Why not just call the built-ins

`digitalWrite(38, true)` requires you to remember three things at once: that 38
is a pin, that it is an output, and that you called `pinOutput(38)` somewhere
earlier. Nothing checks any of it. Pass 38 where you meant 39 and the program
is still valid; forget the setup and it silently does nothing.

An object carries those facts for you:

```dart
final led = OutputPin(38);   // configured as an output, right here
led.value = true;
```

There is no setup step to forget, because constructing the pin *is* the setup.
And `led` is a thing you can pass to a function, store in a list, or hand to a
class — which a bare `38` is not.

## Pins

| | |
|---|---|
| `OutputPin(number)` | `value` (get/set), `toggle()` |
| `InputPin(number, pullUp)` | `value`, `isPressed` |
| `AnalogPin(number)` | `value` (0–4095, or −1 if the pin has no ADC), `fraction` (0.0–1.0), `scaled(low, high)` |
| `PwmPin(number)` | `duty` (0–255), `level` (0.0–1.0) |

`pullUp` holds an input high until something pulls it down — the usual wiring
for a button between the pin and ground. It means an *unpressed* button reads
`true`, which is confusing enough that `isPressed` exists to say what you mean:

```dart
final button = InputPin(11, true);
if (button.isPressed) { ... }
```

`fraction` and `level` are there so programs stop hard-coding 4095 and 255.
A knob driving a lamp is then just:

```dart
lamp.level = knob.fraction;
```

`duty` clamps rather than rejects, so a sensor reading can be fed straight in
without a range check first.

## Buses

```dart
final bus = I2c(8, 9);
if (bus.has(0x48)) {
  print(bus.readRegister(0x48, 0));
}
```

When you are talking to one device, `I2cDevice` saves repeating its address:

```dart
final sensor = I2cDevice(bus, 0x48);
if (sensor.isPresent) print(sensor.read(0));
```

For multi-byte registers, `readBytes(reg, n)` returns a list of up to 64
bytes (empty when the device does not answer) and `writeBytes(reg, bytes)`
writes one — both exist on `I2c` (with an address argument) and on
`I2cDevice`.

`Uart(port, tx, rx, baud)` has `write(text)`, `read()`, `available` and
`hasData`.

`Buzzer(pin)` has `play(hz)`, `stop()`, and `beep(hz, ms)`.

## Servo

```dart
final horn = Servo(18);
horn.write(90);              // degrees, 0..180
horn.writeMicroseconds(1500); // or the pulse width directly
```

`write(degrees)` maps 0..180 onto 1000..2000us — a convention most hobby
servos follow, not a measurement; `writeMicroseconds` (clamped 500..2500) is
the escape hatch when yours doesn't. Two servo channels exist on ESP32; the
six PWM channels behind `PwmPin` and `Buzzer` are a separate, shared pool,
and one more of either is ignored with a warning in the board log.

## Settings that survive a reboot

```dart
final prefs = Prefs();
var boots = prefs.getInt('boots', 0) + 1;
prefs.setInt('boots', boots);
```

Backed by NVS flash on a board and by memory in the simulator. Keys are 1–15
characters; `setInt` returns false when the key is invalid or the store is
full — worth checking on writes you care about, because afterwards a failed
save looks exactly like one that never happened.

## A whole program

```dart
import 'package:moth/hardware.dart';

void main() {
  final button = InputPin(11, true);
  final knob = AnalogPin(4);
  final lamp = PwmPin(5);

  var on = false;
  var wasPressed = false;

  while (true) {
    final pressed = button.isPressed;
    if (pressed && !wasPressed) on = !on;   // act on the press, not the hold
    wasPressed = pressed;

    lamp.level = on ? knob.fraction : 0.0;
    delay(50);
  }
}
```

That is `examples/lamp.dart`, minus an indicator LED on pin 38. Run it
without hardware:

```
$ dart run tools/mothc/bin/mothc.dart examples/lamp.dart
$ ./build/vm/mothrun examples/lamp.mothb --analog 4=2048 --stop-after 200
[     0ms] pin 11 -> input (pull-up)
[     0ms] pin 5 -> output
[     0ms] pin 38 -> output
[     0ms] pin 5 PWM duty 0/255
[    50ms] pin 5 PWM duty 0/255
[   100ms] pin 5 PWM duty 0/255
[   150ms] pin 5 PWM duty 0/255
-- stopped after 200ms (simulated) --
```

## Writing your own

These classes are ordinary Dart in `packages/moth/lib/hardware.dart` — there is
nothing privileged about them. A driver for your own sensor looks the same:

```dart
class Thermometer {
  final I2cDevice device;

  Thermometer(this.device);

  double get celsius => device.read(0) / 2.0;
}
```

Getters are what make this read like Dart rather than like C with extra steps,
and they cost nothing at run time: the VM resolves a property to a field first
and only falls back to a getter when there isn't one.
