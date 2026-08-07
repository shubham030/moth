// OOP without inheritance: composition, duck typing, recursion, chaining.

class Led {
  String name;
  Led(this.name);
  String describe() => 'LED $name';
  int power() => 20;
}

class Motor {
  String name;
  Motor(this.name);
  String describe() => 'motor $name';
  int power() => 900;
}

/// A binary tree — recursive structure with nullable links.
class Node {
  int value;
  Node? left;
  Node? right;
  Node(this.value);

  void insert(int v) {
    if (v < value) {
      if (left == null) {
        left = Node(v);
      } else {
        left!.insert(v);
      }
    } else {
      if (right == null) {
        right = Node(v);
      } else {
        right!.insert(v);
      }
    }
  }

  int sum() {
    var total = value;
    if (left != null) total += left!.sum();
    if (right != null) total += right!.sum();
    return total;
  }

  int depth() {
    var l = left == null ? 0 : left!.depth();
    var r = right == null ? 0 : right!.depth();
    return 1 + (l > r ? l : r);
  }
}

class Builder {
  String out = '';
  Builder add(String part) {
    out = out + part;
    return this; // chaining
  }
}

class Point {
  int x;
  int y;
  Point(this.x, this.y);
  Point shifted(int dx, int dy) => Point(x + dx, y + dy);
  String show() => '($x,$y)';
}

/// Polymorphism with no shared base type: whatever answers describe() works.
void report(List devices) {
  var total = 0;
  for (final d in devices) {
    print('${d.describe()} draws ${d.power()}mA');
    int p = d.power();
    total += p;
  }
  print('total ${total}mA');
}

void main() {
  report([Led('status'), Motor('fan'), Led('backlight')]);

  var root = Node(50);
  root.insert(30);
  root.insert(70);
  root.insert(20);
  root.insert(40);
  print('tree sum ${root.sum()} depth ${root.depth()}');

  print(Builder().add('a').add('b').add('c').out);

  var p = Point(3, 4);
  var q = p.shifted(10, 10);
  print('${p.show()} -> ${q.show()}');

  var r = p;
  print(p == r);           // same object
  print(p == Point(3, 4)); // different object, same contents
  print(p == q);

  print(p.x + q.y);
  print(3 > 4 ? 'no' : 'yes');
}
