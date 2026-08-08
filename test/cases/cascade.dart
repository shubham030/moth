// Cascades. The target is evaluated once, each section runs against it, and
// the whole expression is the target — which is what lets a widget be built
// and configured in a single nested expression instead of a pile of
// assignments to a temporary.

class Pin {
  int number = 0;
  bool high = false;
  var log = [];

  void record(String what) {
    log.add(what);
  }
}

class Counter {
  int n = 0;
  int get doubled => n * 2;
  set bump(int by) {
    n += by;
  }
}

int calls = 0;

Pin make() {
  calls += 1;
  return Pin();
}

void main() {
  var p = Pin()
    ..number = 38
    ..high = true
    ..record('configured');
  print(p.number);
  print(p.high);
  print(p.log);

  // The target is evaluated exactly once, however many sections there are.
  var q = make()
    ..number = 1
    ..number = 2
    ..number = 3;
  print(q.number);
  print(calls);

  // A cascade is an expression, so it can be used where a value is wanted.
  var pins = [
    Pin()..number = 1,
    Pin()..number = 2,
  ];
  print(pins.length);
  print(pins[0].number);
  print(pins[1].number);

  // Sections reach setters as well as fields.
  var c = Counter()
    ..bump = 5
    ..bump = 3;
  print(c.n);
  print(c.doubled);

  // Nested cascades.
  var outer = Pin()
    ..number = 9
    ..log = [Pin()..number = 7];
  print(outer.number);
  print(outer.log[0].number);
}
