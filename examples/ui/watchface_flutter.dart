// The same watch face as watchface.dart, written with Flutter's vocabulary.
//
//   dart run tools/mothc/bin/mothc.dart examples/ui/watchface_flutter.dart
//   ./build/mothsim examples/ui/watchface_flutter.mothb \
//       --size 466x466 --round --frames 30 --shot /tmp/face.ppm
//
// Container, Column, Row, Text, TextStyle, Divider — same names, same
// defaults, same nesting as Flutter. Arc has no Flutter equivalent; it is
// moth's own, and it is what the ring around the bezel is.

import 'package:moth/widgets.dart';

// ---- clock ---------------------------------------------------------------

var epoch = 14 * 3600 + 32 * 60;
var weekday = 'FRI';
var dayOfMonth = 8;
var monthName = 'AUG';

final black = 0xFF000000;
final amber = 0xFFE8A33D;
final dim = 0xFF6B6B76;
final bright = 0xFFF2EFE7;

class Clock {
  int get secondsToday => (epoch + millis() ~/ 1000) % 86400;
  int get hours => secondsToday ~/ 3600;
  int get minutes => (secondsToday ~/ 60) % 60;
  int get seconds => secondsToday % 60;

  /// How far through the current hour, in degrees of a full turn.
  int get minuteSweep => ((minutes * 60 + seconds) * 360) ~/ 3600;

  String two(int n) => n < 10 ? '0$n' : '$n';
  String get display => '${two(hours)}:${two(minutes)}';
  String get date => '$weekday $dayOfMonth $monthName';
}

final clock = Clock();

// ---- the face ------------------------------------------------------------

class WatchFace extends Component {
  bool showSeconds = false;
  int battery = 78;
  bool hasGps = false;

  void tick() => setState(() {});

  Widget build() => Container(
        color: black,
        flex: 1,
        // A Stack, because the rings sit over the face rather than after it.
        // Writing this as a Column worked only because an Arc positions
        // itself absolutely and the column quietly skipped it — which is a
        // lie about what the layout is doing.
        child: Stack(
          children: [
            // The face, inset to the round panel's safe area.
            GestureDetector(
              onTap: () => setState(() => showSeconds = !showSeconds),
              child: Padding(
                padding: uiSafeArea(0),
                child: Column(
                  mainAxisAlignment: mainAxisCenter,
                  crossAxisAlignment: crossAxisCenter,
                  spacing: 18,
                  children: [
                    Text(
                      showSeconds
                          ? '${clock.display}:${clock.two(clock.seconds)}'
                          : clock.display,
                      style: TextStyle(
                          fontSize: showSeconds ? 48 : 72, color: bright),
                    ),
                    Text(clock.date,
                        style: TextStyle(fontSize: 20, color: dim)),
                    Divider(thickness: 2, width: 200, color: 0xFF2A2A31),
                    Row(
                      mainAxisAlignment: mainAxisCenter,
                      crossAxisAlignment: crossAxisCenter,
                      spacing: 14,
                      children: [
                        Text('$battery%',
                            style: TextStyle(fontSize: 18, color: dim)),
                        Container(
                          width: 10,
                          height: 10,
                          borderRadius: 5,
                          color: hasGps ? 0xFF7FD17F : 0xFFE05252,
                        ),
                        Text(hasGps ? 'GPS' : 'NO GPS',
                            style: TextStyle(fontSize: 18, color: dim)),
                      ],
                    ),
                    Text('TAP FOR SECONDS',
                        style: TextStyle(fontSize: 14, color: 0xFF44444E)),
                  ],
                ),
              ),
            ),

            // The track, then the minute hand over it, hugging the bezel.
            Arc(color: 0xFF1C1C21, thickness: 6, sweep: 360, size: uiWidth()),
            Arc(
                color: amber,
                thickness: 6,
                sweep: clock.minuteSweep,
                size: uiWidth()),
          ],
        ),
      );
}

final face = WatchFace();

void main() {
  runApp(face);
  print('flutter-style watch face on ${uiWidth()}x${uiHeight()}');

  var last = millis();
  var shown = -1;
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;

    var s = clock.secondsToday;
    if (s != shown) {
      shown = s;
      face.tick();
    }
    delay(50);
  }
}
