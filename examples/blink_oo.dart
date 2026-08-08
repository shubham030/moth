// The same blink as examples/blink.dart, written with objects.
//
//   dart run tools/mothc/bin/mothc.dart examples/blink_oo.dart
//   ./build/vm/mothrun examples/blink_oo.mothb --stop-after 3000
//
// OutputPin comes from package:moth; Blinker is written here, to show that a
// class of your own composes with the package's the same way it would in any
// Dart program. There is nothing privileged about the ones that ship.

import 'package:moth/hardware.dart';

/// Blinks a pin and remembers how often it has done so.
class Blinker {
  final OutputPin pin;
  final int periodMs;

  int count = 0;

  Blinker(this.pin, this.periodMs);

  void step() {
    pin.toggle();
    count += 1;
    delay(periodMs);
  }

  String report() => 'blinked $count times on pin ${pin.number}';
}

void main() {
  var blinker = Blinker(OutputPin(38), 500);

  for (var i = 0; i < 6; i++) {
    blinker.step();
  }
  print(blinker.report());
}
