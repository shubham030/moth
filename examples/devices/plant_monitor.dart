// Plant monitor — reads soil moisture, runs a pump only when it is dry, and
// refuses to run it too long or too often. The safety rules are the point.

final moisturePin = 4;
final pumpPin = 6;
final dryThreshold = 1800; // ADC counts; lower means drier
final maxPumpMs = 2000;
final minGapMs = 5000;

class Pump {
  int pin;
  bool running = false;
  int startedAt = 0;
  // Far enough in the past that the first watering isn't made to wait.
  int lastStopped = -60000;
  int cycles = 0;

  Pump(this.pin) {
    pinOutput(pin);
  }

  bool mayStart() {
    if (running) return false;
    return millis() - lastStopped >= minGapMs;
  }

  void start() {
    running = true;
    startedAt = millis();
    cycles += 1;
    digitalWrite(pin, true);
  }

  void stop() {
    running = false;
    lastStopped = millis();
    digitalWrite(pin, false);
  }

  /// Hard limit: never leave the pump on longer than maxPumpMs.
  void enforceLimit() {
    if (running && millis() - startedAt >= maxPumpMs) {
      stop();
      print('  pump stopped — hit the ${maxPumpMs}ms safety limit');
    }
  }
}

void main() {
  pinInput(moisturePin);
  var pump = Pump(pumpPin);

  for (var tick = 0; tick < 24; tick++) {
    var moisture = analogRead(moisturePin);
    var dry = moisture < dryThreshold;

    if (dry && pump.mayStart()) {
      pump.start();
      print(
        't=${millis()}ms  moisture $moisture — dry, watering (cycle ${pump.cycles})',
      );
    } else if (!dry && pump.running) {
      pump.stop();
      print('t=${millis()}ms  moisture $moisture — moist enough, stopping');
    }

    pump.enforceLimit();
    delay(500);
  }
  print('finished after ${pump.cycles} watering cycles');
}
