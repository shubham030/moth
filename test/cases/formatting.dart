// print() and '$x' must render exactly as Dart does — lists included, and
// doubles as the shortest text that reads back to the same value.
void main() {
  print([1, 2, 3]);
  print([]);
  print([[1, 2], [3]]);
  print(['a', 'b']);
  print([1, [2, ['x', 3.5]], true, null]);

  print(0.1 + 0.2);
  print(1 / 3);
  print(1.0);
  print(0.5);
  print(123456789.5);
  print(1e21);
  print(-0.0000001234);
  print(2.5 * 4);

  // interpolation must agree with print
  var xs = [1, 2];
  print('list $xs value ${0.1 + 0.2}');
  print('${[true, null]}');
}
