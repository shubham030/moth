// Sample buffers and averaging — the thing scalars alone could never do.

int sum(List<int> values) {
  var total = 0;
  for (final v in values) {
    total += v;
  }
  return total;
}

void main() {
  var readings = [12, 7, 30, 4, 19];
  print(readings.length);
  print(readings[0]);
  print(readings[4]);

  readings[1] = 70;
  print(readings[1]);
  readings[1] += 5;
  print(readings[1]);

  print(sum(readings));
  print(sum(readings) ~/ readings.length);

  var growing = [];
  print(growing.length);
  for (var i = 0; i < 5; i++) {
    growing.add(i * i);
  }
  print(growing.length);
  print(growing[4]);
  print(growing.removeLast());
  print(growing.length);

  // for-in with break and continue
  for (final v in readings) {
    if (v < 10) continue;
    if (v > 60) break;
    print(v);
  }

  // nested lists exercise the collector's recursion
  var grid = [[1, 2], [3, 4], [5, 6]];
  print(grid.length);
  print(grid[1][0]);
  print(grid[2][1]);

  // strings in lists, and text length
  var names = ['sensor', 'fan'];
  print(names[0]);
  print(names[0].length);
  print('joined: ${names[0]} + ${names[1]}');

  growing.clear();
  print(growing.length);

  // churn: many short-lived lists, each holding heap strings
  for (var i = 0; i < 300; i++) {
    var tmp = ['x$i', 'y$i'];
    if (i == 299) print(tmp[1]);
  }
}
