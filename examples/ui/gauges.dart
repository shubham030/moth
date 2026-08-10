// CircularProgressIndicator, at every value.
//
//   dart run tools/mothc/bin/mothc.dart examples/ui/gauges.dart
//   ./build/mothsim examples/ui/gauges.mothb --size 460x200 --frames 30
//
// A tap advances all five, so the ring is watched moving rather than posed.

import 'package:moth/widgets.dart';

final ink = 0xFFF2EFE7;
final dim = 0xFF6B6B76;

class Gauges extends Component {
  /// 0..10, so a tap steps every gauge by a tenth.
  int step = 0;

  double at(double offset) {
    var v = offset + step / 10.0;
    while (v > 1.0) v -= 1.0;
    return v;
  }

  Widget build() => Container(
        color: 0xFF0E0E12,
        flex: 1,
        padding: 16,
        onTap: () => setState(() => step = (step + 1) % 10),
        child: Column(
          mainAxisAlignment: mainAxisCenter,
          crossAxisAlignment: crossAxisCenter,
          spacing: 14,
          children: [
            Row(
              mainAxisAlignment: mainAxisCenter,
              crossAxisAlignment: crossAxisCenter,
              spacing: 14,
              children: [
                // Indeterminate: no value yet, so only the track is drawn.
                CircularProgressIndicator(size: 70, strokeWidth: 8.0),
                CircularProgressIndicator(
                    value: at(0.0), size: 70, strokeWidth: 8.0,
                    color: 0xFF7FD17F),
                CircularProgressIndicator(
                    value: at(0.25), size: 70, strokeWidth: 8.0,
                    color: 0xFFE8A33D),
                CircularProgressIndicator(
                    value: at(0.5), size: 70, strokeWidth: 8.0,
                    color: 0xFFE05252),
                // Butt caps and a stroke sitting inside its box, to show the
                // two knobs Flutter exposes.
                CircularProgressIndicator(
                    value: at(0.75), size: 70, strokeWidth: 8.0,
                    color: 0xFF7AA2F7,
                    strokeCap: strokeCapButt,
                    strokeAlign: strokeAlignInside),
              ],
            ),
            Text('tap to advance — step $step of 10',
                style: TextStyle(fontSize: 14, color: dim)),
          ],
        ),
      );
}

void main() {
  runApp(Gauges());
  var last = millis();
  while (true) {
    var n = millis();
    pumpFrame(n - last);
    last = n;
    delay(16);
  }
}
