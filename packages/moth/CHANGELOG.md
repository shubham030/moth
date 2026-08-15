## 0.1.0

First public release.

- Flutter-shaped widget layer: `Component`, `build()`, `setState`,
  `Container`, `Column`, `Row`, `Stack`, `Center`, `Padding`, `SizedBox`,
  `GestureDetector`, `Text`, `Slider`, `Switch`, `Image`,
  `CircularProgressIndicator`, `Arc`, `Box`, `Divider` — reconciled against a
  native scene graph at 38 fps on an ESP32-S3.
- Hardware as objects: `OutputPin`, `InputPin`, `AnalogPin`, `PwmPin`,
  `Buzzer`, `I2c`, `I2cDevice`, `Uart`, `Prefs` (values that survive
  reboots) and `Servo`, over Arduino-named built-ins — including bulk
  I2C reads and writes (`Wire.requestFrom` parity).
- Every built-in declared `external` with real signatures, so editors
  resolve, complete and type-check moth programs.
