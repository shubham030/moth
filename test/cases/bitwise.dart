void main() {
  print(0xF0 & 0x3C);
  print(0xF0 | 0x0F);
  print(0xFF ^ 0x0F);
  print(~0);
  print(~5);
  print(1 << 8);
  print(0x1234 >> 8);
  print(-16 >> 2);
  print(1 << 62);
  // combining two I2C register bytes into a 16-bit reading
  var hi = 0x12;
  var lo = 0x34;
  print((hi << 8) | lo);
  // masking a status register
  var status = 0x2A;
  print((status & 0x08) != 0);
  print((status & 0x04) != 0);
  var flags = 0;
  flags |= 0x01;
  flags |= 0x10;
  print(flags);
}
