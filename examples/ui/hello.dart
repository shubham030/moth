// The app from the README clip — and the same program `mothc create`
// scaffolds for a new project.
//
// Run it from a checkout:
//
//   moth run examples/ui/hello.dart
//
// A connected board is picked up automatically (the simulator if there is
// none); press r after an edit and the display updates in well under a
// second — no reflashing. See docs/getting-started.md for WiFi pushes.

import 'package:moth/widgets.dart';

class App extends Component {
  int taps = 0;

  @override
  Widget build() {
    return GestureDetector(
      onTap: () {
        setState(() {
          taps += 1;
        });
      },
      child: Container(
        color: 0xFF16161E,
        flex: 1,
        padding: uiSafeArea(0),
        child: Column(
          mainAxisAlignment: mainAxisCenter,
          crossAxisAlignment: crossAxisCenter,
          spacing: 16,
          children: [
            Text('$taps', style: TextStyle(fontSize: 72, color: 0xFFF2EFE7)),
            Text(taps == 0 ? 'tap the screen' : 'keep going',
                style: TextStyle(fontSize: 16, color: 0xFF9AA2B8)),
          ],
        ),
      ),
    );
  }
}

void main() {
  runApp(App());
  var last = millis();
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    delay(16);
  }
}
