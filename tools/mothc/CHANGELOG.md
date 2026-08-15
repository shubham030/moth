## 0.1.0

First public release.

- `mothc app.dart` — compile a Dart subset to a `.mothb` bytecode blob
  (blink is 138 bytes). Unsupported Dart is rejected with a source
  location and a hint, never miscompiled.
- `mothc app.dart --push <serial|host:port>` — hot-push to a running
  board: verified-before-swap, nonce-carrying verdicts, HMAC-paired WiFi
  (`--token`), programs persist across reboots.
- `mothc create <dir>` — scaffold an editor-ready project.
- `mothc check app.dart` — fast subset validation for on-save use.
- `Image('logo.png')` embeds decoded pixels in the program — no
  filesystem needed; a stored program blits them straight from mapped
  flash at no RAM cost.
