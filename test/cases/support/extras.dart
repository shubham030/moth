import 'shapes.dart'; // an import of an import

class Circle extends Shape {
  Circle() { name = 'circle'; }
  String describe() => 'a round ${name}';
}

String shout(String s) => '$s!';
