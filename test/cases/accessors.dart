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

/// A getter and a zero-argument method are the same shape once compiled, so
/// the class member's kind is recorded rather than guessed from arity.
class Mixed {
  int _n = 0;

  int get value => _n;
  set value(int v) {
    _n = v;
  }

  /// Same arity as the getter, and must stay callable.
  int twice() => _n * 2;

  void bump() {
    value++; // ++ through the accessor pair
  }
}

/// A setter with no getter beside it, assigned by bare name from a method.
class WriteOnly {
  int raw = -1;

  set level(int v) {
    raw = v * 10;
  }

  void preset() {
    level = 4;
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

  var m = Mixed();
  m.value = 7;
  print(m.value);
  print(m.twice()); // a method sharing the accessors' arity stays callable
  m.bump();
  print(m.value);

  var w = WriteOnly();
  w.preset();
  print(w.raw);
}
