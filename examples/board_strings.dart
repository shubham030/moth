// Labeled output — the thing numbers-only printing could never do.
final sda = 15;
final scl = 14;

String hex(int v) {
  var digits = '0123456789abcdef';
  return '0x${v ~/ 16}${v % 16}'; // two nibbles by arithmetic — no String.substring yet
}

void main() {
  i2cBegin(sda, scl);
  print('moth on ESP32-S3 — scanning I2C');

  var found = 0;
  for (var addr = 8; addr < 120; addr++) {
    if (i2cPing(addr)) {
      print('  device at address $addr');
      found++;
    }
  }
  print('scan complete: $found devices');

  var uptime = millis();
  print('uptime ${uptime}ms, heap churn test next');

  var acc = '';
  for (var i = 0; i < 500; i++) {
    acc = 'iteration $i of 500';
  }
  print(acc);
  print('done');
}
