// The classic: blink an LED, written in Dart.
//
//   dart run mothc examples/blink.dart
//   mothrun examples/blink.mothb --stop-after 3000

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
