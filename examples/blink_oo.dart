// The same blink, written the way moth is meant to be written: an object
// with a method, not a pile of loose functions taking pin numbers.
//
// This is a sketch of what package:moth will provide — for now the classes
// live in your program, wrapping the low-level built-ins directly.

class DigitalPin {
  int number;
  bool state = false;

  DigitalPin(this.number) {
    pinOutput(number);
  }

  void write(bool value) {
    state = value;
    digitalWrite(number, value);
  }

  void toggle() {
    write(!state);
  }

  void on() {
    write(true);
  }

  void off() {
    write(false);
  }
}

class Blinker {
  DigitalPin pin;
  int periodMs;
  int count = 0;

  Blinker(this.pin, this.periodMs);

  void step() {
    pin.toggle();
    count += 1;
    delay(periodMs);
  }

  String report() {
    return 'blinked ${count} times on pin ${pin.number}';
  }
}

void main() {
  var led = DigitalPin(38);
  var blinker = Blinker(led, 500);

  for (var i = 0; i < 6; i++) {
    blinker.step();
  }
  print(blinker.report());
}
