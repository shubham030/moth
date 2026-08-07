// Classes: fields, constructors with initializing formals, methods,
// implicit this, and objects in lists.

class Counter {
  int value = 0;
  int step;

  Counter(this.step);

  void bump() {
    value += step;      // implicit this on both
  }

  bool past(int limit) {
    return value > limit;
  }

  int doubled() {
    return value * 2;
  }
}

class Reading {
  String label;
  int celsius;

  Reading(this.label, this.celsius);

  String describe() {
    return '$label: ${celsius}C';
  }

  bool hotterThan(Reading other) {
    return celsius > other.celsius;
  }
}

class Empty {
  int slot = 7;
}

void main() {
  var c = Counter(5);
  print(c.step);
  print(c.value);
  c.bump();
  c.bump();
  print(c.value);
  print(c.doubled());
  print(c.past(5));
  print(c.past(50));

  c.value = 100;
  print(c.value);
  c.value += 5;
  print(c.value);

  var a = Reading('fan', 34);
  var b = Reading('case', 21);
  print(a.describe());
  print(b.describe());
  print(a.hotterThan(b));
  print(b.hotterThan(a));

  var e = Empty();
  print(e.slot);

  // objects living in a list, traced by the collector
  var all = [a, b, Reading('cpu', 55)];
  print(all.length);
  for (final r in all) {
    print(r.describe());
  }

  var hottest = all[0];
  for (final r in all) {
    if (r.hotterThan(hottest)) hottest = r;
  }
  print('hottest ${hottest.label}');

  // churn: many short-lived objects holding heap strings
  for (var i = 0; i < 500; i++) {
    var tmp = Reading('probe $i', i);
    if (i == 499) print(tmp.describe());
  }
}
