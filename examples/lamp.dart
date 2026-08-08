// A lamp with a button and a brightness knob, written against package:moth.
//
//   dart run tools/mothc/bin/mothc.dart examples/lamp.dart
//   ./build/vm/mothrun examples/lamp.mothb --analog 4=2048 --stop-after 400
//
// Compare it with examples/blink.dart, which uses the flat built-ins directly.
// Nothing here needs to remember that pin 11 is an input and pin 5 is a PWM
// output: each object knows what it is, and configured itself when it was made.

import 'package:moth/hardware.dart';

void main() {
  final button = InputPin(11, true); // pulled up: pressed reads low
  final knob = AnalogPin(4);
  final lamp = PwmPin(5);
  final indicator = OutputPin(38);

  var on = false;
  var wasPressed = false;

  while (true) {
    // Act on the press, not on the holding down.
    final pressed = button.isPressed;
    if (pressed && !wasPressed) {
      on = !on;
      indicator.value = on;
    }
    wasPressed = pressed;

    // The knob reads 0..4095, but nothing here needs to know that.
    lamp.level = on ? knob.fraction : 0.0;

    delay(50);
  }
}
