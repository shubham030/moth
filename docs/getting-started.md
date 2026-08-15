---
title: Getting started
sidebar_position: 1
slug: /getting-started
---

# Getting started

Two paths. The first needs no hardware at all — start there even if you own a
board, because the loop is faster.

## 1. Install

The quickest taste needs only the [Dart SDK](https://dart.dev/get-dart)
(3.6+):

```console
$ dart pub global activate mothc
$ mothc create hello && cd hello
```

That project opens in any Dart-aware editor with autocomplete working, and
compiles with `mothc app.dart`. To *run* what you compile — the desktop
simulator, the golden tests, or a board — clone the repository; the VM and
renderer are C and build in a minute. You also need CMake plus a C
compiler for that:

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
wrote examples/blink.mothb (138 bytes)

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

For an app with a screen, scaffold a project instead:

```console
$ dart run tools/mothc/bin/mothc.dart create my_app
created my_app/
  app.dart              — the whole app; start in build()
  README.md             — how to run it, desktop and board
  pubspec.yaml          — so your editor resolves package:moth
  analysis_options.yaml — standard Dart lints
  .vscode/tasks.json    — build task runs "mothc check"
  .gitignore            — keeps build output out of git
```

You only ever edit `app.dart`. The rest is for your editor: mothc resolves
`package:moth` by itself and never reads a pubspec, but the Dart analyser
needs one — with it, `my_app` opens with autocomplete, go-to-definition and
type checking on every built-in, because moth's host functions are declared
as `external` for exactly that purpose.

The starter is a tap counter in Flutter's shape (`Component`, `build()`,
`setState`); run it in a window with `make ui F=my_app/app.dart`, and every
push command below works on it unchanged.

## Run it like Flutter

`mothc run` is the loop you know from `flutter run`: it picks the device —
one connected board auto-selects, several prompt you to choose, no board
falls back to the simulator — compiles, pushes, and stays attached
streaming your program's output:

```console
$ mothc run app.dart
Launching app.dart on /dev/cu.usbmodem2101

pushed in 162ms
r  hot restart (recompile + push; state resets)   h  this help   q  quit
```

Press `r` after an edit and the board is running your new code in about
150ms. It is a hot *restart* — the program starts fresh from `main` —
because moth does not preserve state across pushes yet; the prompt says so
rather than borrowing Flutter's "reload". `mothc devices` lists what run
can see; `-d` picks explicitly (`-d sim`, or any unique part of a serial
path).

## What your editor cannot know

The analyser checks ordinary Dart. It does not know which parts of Dart
moth runs — `async`, generics and capturing a local in a closure all look
fine to it and are refused by the compiler. That is what `check` is for:

```console
$ mothc check app.dart
app.dart: ok
```

It compiles and throws the result away, so it is fast enough to run on
every save; in VS Code it is the default build task (Cmd/Ctrl-Shift-B), and
errors land on the right line with the compiler's own hint.

## 4. Put it on a board

You need [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
5.3 or newer. The firmware embeds your compiled program, so compile it first:

```console
$ dart run tools/mothc/bin/mothc.dart examples/board_demo.dart \
    -o vm/esp/main/program.mothb

$ . $HOME/esp/esp-idf/export.sh
$ cd vm/esp
$ idf.py set-target esp32s3
$ idf.py build
$ idf.py -p /dev/cu.usbmodem2101 -b 115200 flash monitor
```

You should see your Dart program running on the chip:

```
I (257) moth: loading 193 bytes of Dart bytecode
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

## 5. The fast loop: push, don't reflash

Flashing is for the first install. After that, the UI firmware
(`ui/esp-s3`) accepts a new program over the same USB cable in well under a
second — your app is bytecode, not a firmware image, so there is nothing to
reflash:

```console
$ dart run tools/mothc/bin/mothc.dart examples/ui/counter.dart \
    --push /dev/cu.usbmodem2101
wrote examples/ui/counter.mothb (2209 bytes)
pushed over /dev/cu.usbmodem2101 in 128ms
```

(128ms measured on an ESP32-S3: compile-to-verdict for a 2.2KB program,
where "pushed" means the board verified the program and confirmed with a
nonce-carrying reply.)

The display never blanks — the running program stops, the new one draws over
it. The pushed program is verified before the running one is disturbed, it
survives reboots, and a program that crashes the board three boots in a row
falls back to the one baked into the firmware.

**Upgrading an older checkout?** The push store needs the custom partition
table this repo now uses, and a generated `sdkconfig` from before it wins
over the new defaults. If the boot log says `no mothb partition`, run
`rm ui/esp-s3/sdkconfig && idf.py reconfigure` and flash again.

To drop the cable entirely, give the board your WiFi once:

```console
$ python3 tools/provision/provision.py --ssid your-network
```

The WiFi password and a **pairing phrase** are prompted, stored only on the
board, and never compiled in. After a reset the board prints its address —
then

```console
$ mothc app.dart --push 192.168.x.x:7621 --token
```

works from anywhere on your network: `--token` asks for the same phrase and
signs the push with it (scripts can set `MOTH_PUSH_TOKEN` instead). A paired
board refuses network pushes that aren't signed — otherwise anyone on your
WiFi could replace what the board is running. Serial pushes never need the
phrase: holding the cable is proof enough. Pairing can be skipped with
`--no-token` at provision time, and the board then warns at boot that its
push port is open.

## 6. Scan a real I2C bus

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

- [Arduino parity](/docs/arduino-parity) — the full list of what works
- [Built-ins](/docs/builtins) — every function you can call
- [Testing](/docs/testing) — how to prove your change didn't break anything
