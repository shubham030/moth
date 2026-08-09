// Named parameters and defaults. Matching happens at compile time, where the
// callee is known — a constructor or a top-level function — so the VM still
// sees an ordinary positional call and they cost nothing at run time.

int area({int w = 1, int h = 1}) => w * h;

String label(String text, {String prefix = '', String suffix = ''}) =>
    '$prefix$text$suffix';

int sum(int a, int b, {int c = 0}) => a + b + c;

class Box {
  int color;
  int pad;
  int corner;
  var kids;

  Box({this.color = 0, this.pad = 0, this.corner = 0, this.kids});

  String describe() => 'Box(color: $color, pad: $pad, corner: $corner)';
}

class Point {
  int x;
  int y;
  Point(this.x, {this.y = 0});
  String describe() => '($x, $y)';
}

void main() {
  // Every combination of supplied and defaulted.
  print(area());
  print(area(w: 3));
  print(area(h: 4));
  print(area(w: 3, h: 4));

  // Order at the call site does not have to match the declaration.
  print(area(h: 4, w: 3));

  print(label('mid'));
  print(label('mid', prefix: '<'));
  print(label('mid', suffix: '>'));
  print(label('mid', suffix: '>', prefix: '<'));

  // Positional and named together.
  print(sum(1, 2));
  print(sum(1, 2, c: 3));

  // Constructors, which is what a widget tree is made of.
  var b = Box(color: 0x101010, pad: 12);
  print(b.describe());
  print(Box().describe());
  print(Box(corner: 8, color: 1).describe());

  var withKids = Box(color: 2, kids: [Box(color: 3), Box(color: 4)]);
  print(withKids.kids.length);
  print(withKids.kids[1].color);

  print(Point(5).describe());
  print(Point(5, y: 9).describe());
}
