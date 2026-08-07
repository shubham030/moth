// Exercises the analog, timing and I2C built-ins against the simulator.
// Run with: --analog 4=1234 --i2c-device 0x5a   (see peripherals.args)

void main() {
  // analog in
  print(analogRead(4));

  // analog out (PWM) — traced, no return value
  analogWrite(5, 0);
  analogWrite(5, 255);

  // sound
  tone(6, 440);
  noTone(6);

  // timing: the virtual clock advances exactly as asked
  print(millis());
  delay(250);
  print(millis());
  delayMicroseconds(1500);
  print(millis());
  print(micros());

  // I2C: 0x5a answers, 0x2b does not
  i2cBegin(15, 14);
  print(i2cPing(90));
  print(i2cPing(43));
  print(i2cWriteReg(90, 1, 200));
  print(i2cReadReg(90, 1));
  print(i2cReadReg(43, 1)); // -1 means no answer

  // UART
  uartBegin(1, 17, 18, 115200);
  uartWrite(1, 65);
  print(uartAvailable(1));
}
