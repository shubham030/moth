// Weather station — reads a temperature sensor over I2C, keeps a rolling
// history, and reports min/max/average with proper labels.

final sensorAddress = 0x48;

class TempSensor {
  int address;

  TempSensor(this.address);

  bool present() {
    return i2cPing(address);
  }

  /// Two registers combined into one signed reading, the usual sensor shape.
  int readCelsius() {
    var high = i2cReadReg(address, 0);
    var low = i2cReadReg(address, 1);
    if (high < 0 || low < 0) return -999;
    var raw = (high << 8) | low;
    if ((raw & 0x8000) != 0) raw -= 65536;
    return raw ~/ 256;
  }
}

class History {
  List<int> samples = [];
  int limit;

  History(this.limit);

  void add(int value) {
    samples.add(value);
    if (samples.length > limit) {
      var trimmed = [];
      for (var i = 1; i < samples.length; i++) {
        trimmed.add(samples[i]);
      }
      samples = trimmed;
    }
  }

  int average() {
    var total = 0;
    for (final s in samples) {
      total += s;
    }
    return total ~/ samples.length;
  }

  int lowest() {
    var low = samples[0];
    for (final s in samples) {
      if (s < low) low = s;
    }
    return low;
  }

  int highest() {
    var high = samples[0];
    for (final s in samples) {
      if (s > high) high = s;
    }
    return high;
  }
}

void main() {
  i2cBegin(15, 14);
  var sensor = TempSensor(sensorAddress);

  if (!sensor.present()) {
    print('no sensor at address $sensorAddress — check the wiring');
    return;
  }
  print('sensor found at $sensorAddress');

  var history = History(5);
  for (var minute = 1; minute <= 6; minute++) {
    history.add(sensor.readCelsius() + random(6) - 3);
    print('t+${minute}m  now ${history.samples[history.samples.length - 1]}C  '
        'avg ${history.average()}C  range ${history.lowest()}..${history.highest()}C');
    delay(200);
  }
}
