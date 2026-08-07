final label = 'temp';

String describe(int c) {
  if (c > 30) return 'hot';
  if (c < 10) return 'cold';
  return 'mild';
}

void main() {
  print('hello');
  print('a' + 'b' + 'c');
  print('$label = 34 C');

  var t = 34;
  var f = 1.5;
  print('int $t double $f bool ${t > 30}');
  print('nested ${describe(t)} reading');

  print('' == '');
  print('abc' == 'ab' + 'c');   // Dart compares text, not identity
  print('abc' == 'abd');
  print('abc' != 'abc');

  print(describe(5));
  print(describe(20));
  print(describe(40));

  // adjacent string literals are one string in Dart
  print('one' 'two');

  // churn: forces many collections, and the result must still be right
  var acc = '';
  for (var i = 0; i < 200; i++) {
    acc = 'x$i';
  }
  print(acc);
  print('$label done');
}
