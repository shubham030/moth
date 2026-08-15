# mothc

The compiler and CLI for [moth](https://github.com/shubham030/moth) —
Flutter's programming model on a $6 microcontroller. Installs two names
for one tool: `moth` (the friendly entry) and `mothc` (the same binary).

```console
$ dart pub global activate mothc

$ moth create hello && cd hello
$ moth run
Launching app.dart on /dev/cu.usbmodem2101

pushed in 162ms
r  hot restart (recompile + push; state resets)   h  this help   q  quit
```

- **Run**: `moth run` picks the device the way flutter run does — one
  connected board auto-selects, several prompt you, the simulator is the
  fallback — then stays attached streaming your program's output. Press
  `r` after an edit and the board runs your new code in ~150ms. It is a
  hot *restart* (state resets); state-preserving reload is on the roadmap.
- **Devices**: `moth devices` lists boards and the simulator. Listing
  never probes a port — probing resets most dev boards.
- **Compile**: `moth app.dart` produces a `.mothb` blob the moth VM runs.
  Dart outside moth's subset is refused at compile time with a hint, and
  `Image('logo.png')` embeds the decoded pixels in the program.
- **Push once**: `--push` swaps the program on a running board over the
  USB cable (no setup) or WiFi (paired with a passphrase at provision
  time); "pushed" means the board verified the program and is running it.
- **Scaffold**: `create` writes a project your editor fully resolves.
- **Check**: `check` reports subset violations fast enough for on-save.

The VM and renderer this compiles for live in the
[moth repository](https://github.com/shubham030/moth) — flashing a board
and the desktop simulator both start there; see the
[getting started guide](https://github.com/shubham030/moth/blob/main/docs/getting-started.md).

Note: compiling a bare `.dart` file with no pubspec resolves
`package:moth` only inside a moth checkout. Projects made by
`moth create` carry the dependency and work anywhere.
