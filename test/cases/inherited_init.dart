// A subclass must run its superclass's field initializers even when it
// declares no constructor of its own. The inheritance golden misses this
// because its classes assign fields before reading them.

class Base {
  int x = 5;
  String tag = 'base';
  List items = [1, 2];
  bool ready = true;
}

class Plain extends Base {}

class Deeper extends Plain {}

class OwnFields extends Base {
  int y = 7;
}

class WithCtor extends Base {
  int z = 0;
  WithCtor(this.z);
}

void main() {
  var p = Plain();
  print('${p.x} ${p.tag} ${p.items} ${p.ready}');

  var d = Deeper(); // two levels up, still no constructor anywhere
  print('${d.x} ${d.tag} ${d.ready}');

  var o = OwnFields();
  print('${o.x} ${o.y} ${o.tag}');

  var w = WithCtor(9);
  print('${w.x} ${w.tag} ${w.z}');

  // the initializers run per instance, not once
  var a = Plain();
  var b = Plain();
  a.x = 99;
  print('${a.x} ${b.x}');
}
