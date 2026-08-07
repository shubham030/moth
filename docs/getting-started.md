---
permalink: /getting-started/
title: Getting started
nav_order: 2
---

# Getting started

Two paths. The first needs no hardware at all — start there even if you own a
board, because the loop is faster.

## 1. Install

You need the [Dart SDK](https://dart.dev/get-dart) (3.6+) for the compiler and
CMake plus a C compiler for the VM.

```console
$ brew install dart-sdk cmake      # macOS; use your package manager elsewhere
$ git clone https://github.com/shubham030/moth.git
$ cd moth
```

Build the VM and the desktop runner:

```console
$ cmake -B vm/build vm && cmake --build vm/build
```

That produces `vm/build/mothrun`. Fetch the compiler's dependencies once:

```console
$ dart pub get --directory tools/mothc
```

## 2. Run a program with no hardware

```console
$ dart run tools/mothc/bin/mothc.dart examples/blink.dart
wrote examples/blink.mothb (130 bytes)

$ ./vm/build/mothrun examples/blink.mothb --stop-after 3000
[     0ms] pin 38 -> output
[     0ms] pin 38 = HIGH
[   500ms] pin 38 = low
[  1000ms] pin 38 = HIGH
[  1500ms] pin 38 = low
[  2000ms] pin 38 = HIGH
[  2500ms] pin 38 = low
-- stopped after 3000ms (simulated) --
```

`mothrun` simulates the pins against a **virtual clock**, so three seconds of
blinking finish instantly. Pass `--real-time` if you want it to actually wait.

Useful flags:

| Flag | Effect |
| --- | --- |
| `--stop-after MS` | halt once the simulated clock reaches MS |
| `--real-time` | sleep for real on `delay()` |
| `--quiet` | suppress the pin/bus trace, leaving only `print()` |
| `--analog PIN=VAL` | what `analogRead(PIN)` should return |
| `--i2c-device ADDR` | pretend a device answers at that address |
| `--seed N` | seed `random()` |

## 3. Write your own

Create `hello.dart`:

```dart
int double(int n) {
  return n * 2;
}

void main() {
  for (var i = 1; i <= 5; i++) {
    print(double(i));
  }
}
```

```console
$ dart run tools/mothc/bin/mothc.dart hello.dart && ./vm/build/mothrun hello.mothb
2
4
6
8
10
```

If you make a mistake, the compiler points at it:

```
hello.dart:3:10: 'x' is not defined
    return x * 2;
           ^
  hint: only local variables exist in M1a — declare it with "var x = ...;"
```

## 4. Put it on a board

You need [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
5.3 or newer. The firmware embeds your compiled program, so compile it first:

```console
$ dart run tools/mothc/bin/mothc.dart examples/board_demo.dart \
    -o vm/esp/main/program.mothb

$ . $HOME/esp/esp-idf/export.sh
$ cd vm/esp
$ idf.py set-target esp32s3          # or esp32p4
$ idf.py build
$ idf.py -p /dev/cu.usbmodem2101 -b 115200 flash monitor
```

You should see your Dart program running on the chip:

```
I (257) moth: loading 185 bytes of Dart bytecode
I (262) moth: running Dart on the VM
I (273) moth: pin 21 -> output
I (276) moth: 0
I (277) moth: pin 21 = HIGH
I (1280) moth: 1
I (1280) moth: pin 21 = low
```

{: .note }
If flashing fails with "serial data stream stopped", drop the baud rate
(`-b 115200`). If the port is missing, check `ls /dev/cu.*` — it changes when
the board re-enumerates.

## 5. Scan a real I2C bus

`examples/i2c_scan.dart` walks every address and prints the ones that answer —
a good first test that your wiring works:

```dart
void main() {
  i2cBegin(15, 14); // sda, scl — use your board's pins
  var found = 0;
  for (var addr = 8; addr < 120; addr++) {
    if (i2cPing(addr)) {
      print(addr);
      found++;
    }
  }
  print(-1);
  print(found);
}
```

On a Waveshare ESP32-S3-Touch-AMOLED this reports eight devices, including the
touch controller at 0x5A.

## Next

- [Arduino parity]({{ site.baseurl }}/arduino-parity/) — the full list of what works
- [Built-ins]({{ site.baseurl }}/builtins/) — every function you can call
- [Testing]({{ site.baseurl }}/testing/) — how to prove your change didn't break anything
