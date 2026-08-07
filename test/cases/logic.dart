bool yes() {
  print(1);
  return true;
}

bool no() {
  print(0);
  return false;
}

void main() {
  print(no() && yes()); // short-circuits: yes() must not run
  print(yes() || no()); // short-circuits: no() must not run
  print(true && false);
  print(!(3 > 4));
  print(3 <= 3 && 4 >= 5);
}
