# package:moth

The Dart library for [moth](https://github.com/shubham030/moth) —
Flutter's programming model on a $6 microcontroller.

```dart
import 'package:moth/widgets.dart';

class Counter extends Component {
  int count = 0;

  @override
  Widget build() {
    return Container(
      color: 0xFF1A1B26,
      flex: 1,
      onTap: () => setState(() => count += 1),
      child: Center(
        child: Text('tapped $count times',
            style: TextStyle(fontSize: 20, color: 0xFFF2EFE7)),
      ),
    );
  }
}
```

This package is not a normal Dart dependency: programs that import it are
compiled by [`mothc`](https://pub.dev/packages/mothc) to compact bytecode
and interpreted on-device by the moth VM. Depending on it from a pubspec is
what lets your **editor** resolve the imports — autocomplete, types and
go-to-definition all work, because every host function is declared
`external` with its real signature.

moth runs a practical subset of Dart (no `async`, closures capture only
`this`, generic annotations are erased). The compiler reports anything
outside the subset with a source location and a hint; `mothc check
app.dart` runs those checks on every save.

Start at the [getting started guide](https://github.com/shubham030/moth/blob/main/docs/getting-started.md).
`mothc create my_app` scaffolds a project with this package wired up.

Verified hardware today: ESP32-S3 with a Waveshare 1.75" round AMOLED.
The API is unstable until 1.0.
