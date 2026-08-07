// Closures that capture nothing, and closures that capture `this`.
var taps = 0;
var log = [];

class Counter {
  int count = 0;
  String name;

  Counter(this.name);

  // returns a closure over `this`
  Function bumper() {
    return () {
      count += 1;
      log.add('$name -> $count');
    };
  }

  Function adder() {
    return (int by) {
      count += by;
      return count;
    };
  }
}

int applyTwice(Function f, int start) {
  return f(f(start));
}

void main() {
  // captures nothing
  var tick = () {
    taps += 1;
  };
  tick();
  tick();
  print(taps);

  // functions as values in a list
  var handlers = [];
  handlers.add(() => 'first');
  handlers.add(() => 'second');
  for (final h in handlers) {
    print(h());
  }
  print(handlers[1]());

  // captures `this`
  var a = Counter('a');
  var b = Counter('b');
  var bumpA = a.bumper();
  var bumpB = b.bumper();
  bumpA();
  bumpA();
  bumpB();
  print(a.count);
  print(b.count);
  print(log);

  // arguments and return values
  var add = a.adder();
  print(add(10));
  print(a.count);

  // a function passed to a function
  print(applyTwice((int n) => n * 3, 2));

  // closures kept in fields, called later
  var pending = [bumpA, bumpB, tick];
  for (final p in pending) {
    p();
  }
  print(a.count);
  print(b.count);
  print(taps);
}
