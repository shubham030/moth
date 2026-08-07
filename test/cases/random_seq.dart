// random() is deterministic for a given seed, so tests can assert on it.
void main() {
  randomSeed(42);
  for (var i = 0; i < 5; i++) {
    print(random(100));
  }
  randomSeed(42);
  print(random(100)); // same seed -> same first value
}
