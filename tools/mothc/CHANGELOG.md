## 0.1.0

First public release. One tool, two installed names: `moth` and `mothc`.

- `moth run [app.dart] [-d DEVICE]` — the flutter-run loop: device
  auto-selection (one board wins, several prompt, simulator fallback),
  compile + push + attach with the program's output streaming, `r` = hot
  restart (~173ms over the cable, measured), `q` = quit. Compile errors
  during `r` keep the old program running.
- `moth devices` — boards and the simulator, without probing (probing
  resets most dev boards).
- `moth app.dart` — compile a Dart subset to a `.mothb` blob (blink is
  138 bytes). Unsupported Dart is rejected with a source location and a
  hint, never miscompiled. `Image('logo.png')` embeds decoded pixels;
  the board blits them with no filesystem — a stored program straight
  from mapped flash at no RAM cost.
- `moth app.dart --push <serial|host:port>` — one-shot hot-push:
  verified-before-swap, nonce-carrying verdicts, HMAC-paired WiFi
  (`--token`), programs persist across reboots.
- `moth create <dir>` — scaffold an editor-ready project.
- `moth check app.dart` — fast subset validation for on-save use.
