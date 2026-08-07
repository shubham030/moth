// Smoothing a noisy sensor with a rolling window — the classic thing you
// cannot do without lists, and the reason M1b matters.
//
//   mothrun examples/rolling_average.mothb --quiet --analog 4=2600 --stop-after 3000

final sensorPin = 4;
final windowSize = 8;

var window = [];

/// Keeps the last [windowSize] readings, dropping the oldest.
void record(int reading) {
  window.add(reading);
  if (window.length > windowSize) {
    // shift left by one — no removeAt yet, so rebuild
    var shifted = [];
    for (var i = 1; i < window.length; i++) {
      shifted.add(window[i]);
    }
    window = shifted;
  }
}

int average() {
  if (window.length == 0) return 0;
  var total = 0;
  for (final v in window) {
    total += v;
  }
  return total ~/ window.length;
}

int spread() {
  if (window.length == 0) return 0;
  var low = window[0];
  var high = window[0];
  for (final v in window) {
    if (v < low) low = v;
    if (v > high) high = v;
  }
  return high - low;
}

void main() {
  pinInput(sensorPin);

  for (var tick = 0; tick < 20; tick++) {
    // real ADCs jitter; the simulator returns a fixed value, so add some
    var noise = random(40) - 20;
    record(analogRead(sensorPin) + noise);

    if (window.length == windowSize) {
      print('avg ${average()} spread ${spread()} over ${window.length} samples');
    }
    delay(100);
  }
}
