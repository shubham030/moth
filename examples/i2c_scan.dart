// Scan the I2C bus and report every device that answers — the embedded
// equivalent of "hello world", written in Dart.
//
// Pins are the Waveshare ESP32-S3-Touch-AMOLED-1.75C defaults; change them
// for your board.

void main() {
  var sda = 15;
  var scl = 14;
  i2cBegin(sda, scl);

  var found = 0;
  for (var addr = 8; addr < 120; addr++) {
    if (i2cPing(addr)) {
      print(addr); // decimal; 0x18 shows as 24
      found++;
    }
  }
  print(-1); // marker: scan complete
  print(found);
}
