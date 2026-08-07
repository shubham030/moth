// Top-level variables: constants shared by every function, plus mutable state
// that survives across calls.
final ledPin = 38;
final threshold = 100;
var counter = 0;
var total = 0;

int bump(int by) {
  counter++;
  total += by;
  return total;
}

bool overThreshold() {
  return total > threshold;
}

void main() {
  print(ledPin);
  print(counter);

  print(bump(30));
  print(bump(40));
  print(overThreshold());
  print(bump(50));
  print(overThreshold());
  print(counter);

  // locals shadow top-level names
  var threshold = 5;
  print(threshold);
}
