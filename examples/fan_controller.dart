// A realistic controller, written in what moth supports today: reads a
// thermistor, drives a PWM fan with hysteresis, lets a button force full
// speed, and reports once a second without ever blocking the loop.
//
//   dart run tools/mothc/bin/mothc.dart examples/fan_controller.dart
//   mothrun examples/fan_controller.mothb --quiet --analog 4=2600 --stop-after 4000

// Pin map and tuning, visible to every function in the file.
final sensorPin = 4;
final fanPin = 6;
final buttonPin = 0;
final statusLed = 21;

final startTempC = 28; // fan kicks in here
final stopTempC = 25; // and off again here — the gap prevents chattering
final fullTempC = 40; // full speed by here

var running = false;
var lastReport = 0;

int clamp(int v, int low, int high) {
  if (v < low) return low;
  if (v > high) return high;
  return v;
}

int mapRange(int v, int inLow, int inHigh, int outLow, int outHigh) {
  return (v - inLow) * (outHigh - outLow) ~/ (inHigh - inLow) + outLow;
}

/// 12-bit ADC reading to degrees C, for a sensor spanning -10..60.
int readTempC() {
  return mapRange(analogRead(sensorPin), 0, 4095, -10, 60);
}

/// Hysteresis: two thresholds instead of one, so the fan doesn't chatter.
void updateRunning(int tempC) {
  if (!running && tempC >= startTempC) running = true;
  if (running && tempC <= stopTempC) running = false;
}

int fanDuty(int tempC, bool boost) {
  if (boost) return 255;
  if (!running) return 0;
  return clamp(mapRange(tempC, stopTempC, fullTempC, 60, 255), 0, 255);
}

void main() {
  pinInput(sensorPin);
  pinOutput(fanPin);
  pinOutput(statusLed);
  pinInputPullup(buttonPin);

  while (true) {
    var boost = !digitalRead(buttonPin);
    var tempC = readTempC();

    updateRunning(tempC);
    analogWrite(fanPin, fanDuty(tempC, boost));
    digitalWrite(statusLed, running);

    // Non-blocking report — the loop keeps servicing the button meanwhile.
    if (millis() - lastReport >= 1000) {
      print(tempC);
      lastReport = millis();
    }

    delay(100);
  }
}
