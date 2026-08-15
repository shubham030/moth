## 0.1.0

First public release.

- Flutter-shaped widget layer: `Component`, `build()`, `setState`,
  `Container`, `Column`, `Row`, `Stack`, `Center`, `Padding`, `SizedBox`,
  `GestureDetector`, `Text`, `Slider`, `Switch`, `Image`,
  `CircularProgressIndicator`, `Arc`, `Divider` — reconciled against a
  native scene graph at 38 fps on an ESP32-S3.
- Hardware as objects: `OutputPin`, `InputPin`, `AnalogPin`, `PwmPin`,
  `Buzzer`, `I2c`, `I2cDevice`, `Uart` over Arduino-named built-ins.
- Every built-in declared `external` with real signatures, so editors
  resolve, complete and type-check moth programs.
