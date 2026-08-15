# mothc

The compiler and CLI for [moth](https://github.com/shubham030/moth) —
Flutter's programming model on a $6 microcontroller.

```console
$ dart pub global activate mothc

$ mothc create my_app
$ cd my_app
$ mothc app.dart --push /dev/cu.usbmodemXXXX
pushed over /dev/cu.usbmodemXXXX in 180ms
```

- **Compile**: `mothc app.dart` produces a `.mothb` blob the moth VM runs.
  Dart outside moth's subset is refused at compile time with a hint.
- **Push**: `--push` swaps the program on a running board over the USB
  cable (no setup) or WiFi (paired with a passphrase at provision time);
  "pushed" means the board verified the program and is running it.
- **Scaffold**: `create` writes a project your editor fully resolves.
- **Check**: `check` reports subset violations fast enough for on-save.

The VM and renderer this compiles for live in the
[moth repository](https://github.com/shubham030/moth) — flashing a board
and the desktop simulator both start there; see the
[getting started guide](https://github.com/shubham030/moth/blob/main/docs/getting-started.md).

Note: compiling a bare `.dart` file with no pubspec resolves
`package:moth` only inside a moth checkout. Projects made by
`mothc create` carry the dependency and work anywhere.
