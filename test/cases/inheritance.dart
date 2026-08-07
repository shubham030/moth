// Single inheritance, flattened at compile time. Because dispatch is by name
// at run time, an inherited method that calls an overridden one lands on the
// override — virtual dispatch with no runtime machinery.

class Shape {
  String name = 'shape';
  int sides = 0;

  String describe() => '$name with $sides sides';
  String announce() => 'this is a ${describe()}';
  int doubled() => sides * 2;
}

class Polygon extends Shape {
  int corners = 0;

  void configure(int n) {
    sides = n;      // inherited field
    corners = n;    // own field
    name = 'polygon';
  }

  String describe() => '$name with $sides sides and $corners corners';
}

class Square extends Polygon {
  String describe() => 'square';
  int doubled() => 8;
}

void main() {
  var s = Shape();
  var p = Polygon();
  var q = Square();

  p.configure(5);
  q.configure(4);

  print(s.describe());
  print(p.describe());
  print(q.describe());

  // announce() is inherited by both, and calls the right override
  print(s.announce());
  print(p.announce());
  print(q.announce());

  // inherited fields survive two levels down
  print(q.sides);
  print(q.corners);
  print(q.name);

  // inherited method not overridden, and one that is
  print(p.doubled());
  print(q.doubled());

  // polymorphism over a mixed list
  var all = [s, p, q];
  for (final x in all) {
    print('${x.describe()} / ${x.doubled()}');
  }
}
