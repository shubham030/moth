// Read a button, light an LED while it is held.
// The button pin uses a pull-up, so it reads false (low) when pressed.

void main() {
  var button = 0;
  var led = 38;
  pinInputPullup(button);
  pinOutput(led);

  while (true) {
    var pressed = !digitalRead(button);
    digitalWrite(led, pressed);
    delay(50);
  }
}
