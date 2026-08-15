// Bulk I2C, prefs, and servo — the parity additions, exercised through
// BOTH layers: the raw natives and the package:moth classes over them.
// The fake device at 0x50 has a register file; writeBytes/readBytes must
// round-trip through it. Prefs are in-memory here (NVS on a board), so
// the second read sees the first write.

import 'package:moth/hardware.dart';

void main() {
  final bus = I2c(8, 9);

  print(bus.writeBytes(0x50, 0x10, [11, 22, 33]));
  print(bus.readBytes(0x50, 0x10, 3));
  print(bus.readBytes(0x22, 0x00, 2)); // nobody home: empty list

  final sensor = I2cDevice(bus, 0x50);
  print(sensor.readBytes(0x10, 2));

  final prefs = Prefs();
  print(prefs.getInt('boots', -1)); // absent: the fallback
  print(prefs.setInt('boots', 41));
  print(prefs.getInt('boots', -1) + 1);
  print(prefs.setInt('a-key-far-too-long-for-nvs', 1)); // invalid: false

  final servo = Servo(4);
  servo.writeMicroseconds(1500);
  servo.write(90); // the class maps degrees to pulse width
  servoMicroseconds(4, 9999); // the native clamps to 2500
}
