// Interpreter throughput, measured on whatever host runs it. The last two
// cases are the ones M3 depends on: rebuilding a tree of objects and diffing
// it, which is what setState does on every state change.

int fib(int n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

class Node {
  int a = 0;
  int b = 0;
  String label = '';

  int sum() => a + b;

  void set(int x, int y) {
    a = x;
    b = y;
  }
}

void report(String name, int ops, int ms) {
  if (ms <= 0) ms = 1;
  print('$name: $ops ops in ${ms}ms = ${ops ~/ ms} k-ops/sec');
}

void main() {
  var t = millis();
  var loops = 0;
  for (var i = 0; i < 300000; i++) {
    loops += i;
  }
  report('empty loop + add', 300000, millis() - t);

  t = millis();
  print('  fib(22) = ${fib(22)}');
  report('function calls (fib 22)', 57000, millis() - t);

  var node = Node();
  t = millis();
  for (var i = 0; i < 200000; i++) {
    node.a = i;
  }
  report('field writes', 200000, millis() - t);

  t = millis();
  var acc = 0;
  for (var i = 0; i < 200000; i++) {
    acc += node.a;
  }
  report('field reads', 200000, millis() - t);

  t = millis();
  for (var i = 0; i < 100000; i++) {
    node.set(i, i);
  }
  report('method calls', 100000, millis() - t);

  var xs = [];
  for (var i = 0; i < 1000; i++) {
    xs.add(i);
  }
  t = millis();
  var total = 0;
  for (var round = 0; round < 100; round++) {
    for (var i = 0; i < xs.length; i++) {
      total += xs[i];
    }
  }
  report('list index reads', 100000, millis() - t);

  t = millis();
  for (var i = 0; i < 20000; i++) {
    var s = 'value $i units';
    if (s.length == 0) print('never');
  }
  report('string build (interpolation)', 20000, millis() - t);

  // The M3 shape: allocate a tree of widget-sized objects, then walk it.
  t = millis();
  var built = 0;
  for (var round = 0; round < 200; round++) {
    var tree = [];
    for (var i = 0; i < 30; i++) {
      var n = Node();
      n.set(i, round);
      n.label = 'w$i';
      tree.add(n);
      built += 1;
    }
    // diff pass: compare each against what it would replace
    for (final n in tree) {
      if (n.sum() < 0) print('never');
    }
  }
  report('rebuild+diff (30 nodes)', built, millis() - t);
  print('done');
}
