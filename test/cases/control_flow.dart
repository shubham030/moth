void main() {
  for (var i = 0; i < 5; i++) {
    if (i == 2) continue;
    if (i == 4) break;
    print(i);
  }
  var n = 3;
  while (n > 0) {
    print(n);
    n--;
  }
  if (n == 0) {
    print(100);
  } else {
    print(200);
  }
}
