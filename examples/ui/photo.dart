// Images, three ways: intrinsic size, scaled up, and rounded corners.
// The PNG is embedded by the compiler — the path is relative to this file,
// and the board never needs a filesystem.
//
//   make ui F=examples/ui/photo.dart

import 'package:moth/widgets.dart';

class Photo extends Component {
  Widget build() {
    return Container(
      color: 0xFF16161E,
      flex: 1,
      padding: uiSafeArea(0),
      child: Column(
        mainAxisAlignment: mainAxisCenter,
        crossAxisAlignment: crossAxisCenter,
        spacing: 18,
        children: [
          Image('img/moth64.png'),
          Image('img/moth64.png', width: 128, height: 128),
          Image('img/moth64.png', width: 96, height: 96, borderRadius: 24),
          Text('one PNG, embedded at compile time',
              style: TextStyle(fontSize: 14, color: 0xFF9AA2B8)),
        ],
      ),
    );
  }
}

void main() {
  runApp(Photo());
  var last = millis();
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    delay(30);
  }
}
