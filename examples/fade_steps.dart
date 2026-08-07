// Loops, functions and arithmetic — no hardware needed to try this one.

int square(int n) {
  return n * n;
}

void main() {
  for (var i = 1; i <= 5; i++) {
    print(square(i));
  }

  var total = 0;
  var n = 1;
  while (n <= 10) {
    if (n % 2 == 0) {
      total += n;
    }
    n++;
  }
  print(total); // 2+4+6+8+10
}
