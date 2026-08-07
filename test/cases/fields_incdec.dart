// ++ and -- on fields, through implicit this and through an object.
class Counter {
  int count = 0;
  int other = 100;

  void tick() {
    count++;
    ++count;
    other--;
  }

  int bumpAndGet() => count++;
  int getAndBump() => ++count;
}

void main() {
  var c = Counter();
  c.tick();
  print('${c.count} ${c.other}');
  print(c.bumpAndGet());
  print(c.count);
  print(c.getAndBump());
  print(c.count);
  c.count++;
  print(c.count);
  print(c.count++);
  print(c.count);
  print(++c.count);
  c.count -= 3;
  print(c.count);
}
