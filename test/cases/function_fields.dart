// A function stored in a field is callable like a method, as in Dart.
var log = [];

class Button {
  String name;
  Function handler;

  Button(this.name, this.handler);

  void fire() {
    handler(); // field call through implicit this
  }

  String describe() => 'button $name';
}

void run(List buttons) {
  for (final b in buttons) {
    b.handler(); // field call on a value of unknown type
  }
}

void main() {
  var a = Button('a', () { log.add('a'); });
  var b = Button('b', () { log.add('b'); });

  a.handler(); // field call from outside
  b.fire();
  run([a, b]);

  print(log);
  print(a.describe());

  // a method and a field of the same shape resolve method-first
  print(b.describe());
}
