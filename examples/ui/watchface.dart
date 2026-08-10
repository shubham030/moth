// A watch face for the 466x466 round panel.
//
//   dart run tools/mothc/bin/mothc.dart examples/ui/watchface.dart
//   ./build/mothsim examples/ui/watchface.mothb --size 466x466 --round \
//       --frames 30 --shot /tmp/face.ppm
//
// The ring around the edge is the minute hand: a full sweep is one hour. The
// time comes from a base set at startup plus millis(), so it runs in the
// simulator with nothing attached; on a board you would set `epoch` from an
// RTC over I2C once at boot and let it run from there.

import 'package:moth/widgets.dart';

// ---- clock ---------------------------------------------------------------

/// Seconds since midnight when the program started.
var epoch = 14 * 3600 + 32 * 60;

/// Day of the month and month name, since there is no calendar to ask.
var dayOfMonth = 8;
var monthName = 'AUG';
var weekday = 'FRI';

final black = 0xFF000000;
final palette = 0xFFE8A33D; // the ring, and anything that should catch the eye
final dim = 0xFF6B6B76;
final bright = 0xFFF2EFE7;

class Clock {
  int get secondsToday => (epoch + millis() ~/ 1000) % 86400;
  int get hours => secondsToday ~/ 3600;
  int get minutes => (secondsToday ~/ 60) % 60;
  int get seconds => secondsToday % 60;

  /// How far through the current hour, as degrees of a full turn.
  int get minuteSweep => ((minutes * 60 + seconds) * 360) ~/ 3600;

  String two(int n) => n < 10 ? '0$n' : '$n';
  String get display => '${two(hours)}:${two(minutes)}';
  String get date => '$weekday $dayOfMonth $monthName';
}

final clock = Clock();

// ---- the face ------------------------------------------------------------

class WatchFace extends Component {
  bool showSeconds = false;

  /// The clock moves on its own, so the face rebuilds when the second does —
  /// not only when something is tapped.
  void tick() {
    setState(() {});
  }

  /// Stand-ins for real hardware: on a board these read a divider and a GPS.
  int battery = 78;
  bool hasGps = false;

  Widget build() {
    // A Stack: the rings sit over the face rather than after it. An Arc lays
    // out like any other widget now, so overlaying has to be asked for.
    return Box()
      ..color = black
      ..growFactor = 1
      ..direction = directionStack
      ..kids = [
        // The face itself, inset to the round panel's safe area.
        Box()
          ..color = black
          ..growFactor = 1
          ..pad = uiSafeArea(0)
          ..space = 18
          ..align = alignCenter
          ..crossAlign = alignCenter
          ..onTap = () {
            setState(() {
              showSeconds = !showSeconds;
            });
          }
          ..kids = [
            Text(
              showSeconds
                  ? '${clock.display}:${clock.two(clock.seconds)}'
                  : clock.display,
              style: TextStyle(
                  fontSize: showSeconds ? 48 : 72, color: bright),
            ),
            Text(clock.date, style: TextStyle(fontSize: 20, color: dim)),
            Box()
              ..color = 0xFF2A2A31
              ..fixedHeight = 2
              ..fixedWidth = 200,
            Box()
              ..direction = directionRow
              ..space = 14
              ..align = alignCenter
              ..crossAlign = alignCenter
              ..kids = [percent(), dot(), gpsLabel()],
            Text('TAP FOR SECONDS', style: TextStyle(fontSize: 14, color: 0xFF44444E)),
          ],

        // Track and minute hand, drawn in one pass over the face.
        Arc()
          ..color = palette
          ..trackColor = 0xFF1C1C21
          ..thickness = 6
          ..sweep = clock.minuteSweep
          ..size = uiWidth(),
      ];
  }

  Widget percent() => Text('$battery%', style: TextStyle(fontSize: 18, color: dim));

  Widget dot() => Box()
    ..color = hasGps ? 0xFF7FD17F : 0xFFE05252
    ..fixedWidth = 10
    ..fixedHeight = 10
    ..corner = 5; // a real circle now that radius is drawn

  Widget gpsLabel() => Text(hasGps ? 'GPS' : 'NO GPS', style: TextStyle(fontSize: 18, color: dim));
}

final face = WatchFace();

void main() {
  runApp(face);
  print('watch face on ${uiWidth()}x${uiHeight()}');

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
