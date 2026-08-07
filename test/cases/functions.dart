int fib(int n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

int add(int a, int b) => a + b;

void main() {
  print(fib(15));
  print(add(20, 22));
  for (var i = 0; i < 5; i++) {
    print(fib(i));
  }
}
