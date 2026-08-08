// Declarations come from other files, including one reached only indirectly.
import 'support/shapes.dart';
import 'support/extras.dart';

class Square extends Shape {
  Square() { name = 'square'; }
}

void main() {
  print(libraryName);
  print(twice(21));
  print(shout('hello'));

  var s = Shape();
  var c = Circle();
  var q = Square();
  print(s.describe());
  print(c.describe());
  print(q.describe());

  // inheritance across files, dispatched by name
  var all = [s, c, q];
  for (final x in all) {
    print(x.describe());
  }
}
