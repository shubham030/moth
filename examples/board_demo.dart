// Runs on real hardware: a user-defined function, arithmetic, a GPIO pin and
// a timed loop — all in Dart, interpreted on the microcontroller itself.

int square(int n) {
  return n * n;
}

void main() {
  var led = 21;
  pinOutput(led);

  var i = 0;
  while (true) {
    print(square(i));
    digitalWrite(led, i % 2 == 0);
    delay(1000);
    i++;
  }
}
