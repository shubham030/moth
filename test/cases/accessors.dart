// Getters and setters. They compile to ordinary methods sharing the property's
// name; the VM tells a getter from a setter by arity, since a getter takes only
// the receiver. A bare name inside a method reaches them through `this`.

class Thermo {
  int raw = 0;

  int get celsius => raw ~/ 10;
  set celsius(int c) {
    raw = c * 10;
  }

  bool get isFreezing => celsius <= 0;

  String describe() => '$celsius degrees';
}

class Counter {
  int _n = 0;

  int get value => _n;
  set value(int v) {
    _n = v < 0 ? 0 : v; // a setter may reject what it is given
  }

  void bump() {
    value = value + 1;
  }
}

void main() {
  var t = Thermo();
  t.celsius = 25;
  print(t.raw);
  print(t.celsius);
  print(t.isFreezing);
  print(t.describe());

  t.celsius = -4;
  print(t.celsius);
  print(t.isFreezing);

  // An assignment is an expression, and evaluates to the assigned value.
  print(t.celsius = 7);
  print(t.raw);

  var c = Counter();
  c.bump();
  c.bump();
  print(c.value);
  c.value = -100;
  print(c.value);
}
