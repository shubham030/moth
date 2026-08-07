// A realistic controller, written entirely in what moth supports today:
// reads a thermistor, drives a PWM fan with hysteresis, lets a button force
// full speed, and reports once a second without ever blocking the loop.
//
//   dart run tools/mothc/bin/mothc.dart examples/fan_controller.dart
//   mothrun examples/fan_controller.mothb --quiet --analog 4=2600 --stop-after 4000

int clamp(int v, int low, int high) {
  if (v < low) return low;
  if (v > high) return high;
  return v;
}

int mapRange(int v, int inLow, int inHigh, int outLow, int outHigh) {
  return (v - inLow) * (outHigh - outLow) ~/ (inHigh - inLow) + outLow;
}

/// 12-bit ADC reading to degrees C, for a sensor spanning -10..60.
int readTempC(int pin) {
  return mapRange(analogRead(pin), 0, 4095, -10, 60);
}

/// Fan curve: off below the low mark, ramping to full by 40 degrees.
int fanDuty(int tempC, bool running, bool boost) {
  if (boost) return 255;
  if (!running) return 0;
  return clamp(mapRange(tempC, 25, 40, 60, 255), 0, 255);
}

void main() {
  var sensorPin = 4;
  var fanPin = 6;
  var buttonPin = 0;
  var statusLed = 21;

  pinInput(sensorPin);
  pinOutput(fanPin);
  pinOutput(statusLed);
  pinInputPullup(buttonPin);

  var running = false;
  var lastReport = 0;

  while (true) {
    var boost = !digitalRead(buttonPin);
    var tempC = readTempC(sensorPin);

    // Hysteresis: start at 28, stop at 25, so it doesn't chatter at the edge.
    if (!running && tempC >= 28) running = true;
    if (running && tempC <= 25) running = false;

    var duty = fanDuty(tempC, running, boost);
    analogWrite(fanPin, duty);
    digitalWrite(statusLed, running);

    // Non-blocking report — the loop keeps servicing the button meanwhile.
    if (millis() - lastReport >= 1000) {
      print(tempC);
      print(duty);
      lastReport = millis();
    }

    delay(100);
  }
}
