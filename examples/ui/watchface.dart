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
    final inset = uiSafeArea(0);

    var time = Text();
    time.value = showSeconds ? '${clock.display}:${clock.two(clock.seconds)}' : clock.display;
    time.size = showSeconds ? 48 : 72;
    time.tint = bright;

    var date = Text();
    date.value = clock.date;
    date.size = 20;
    date.tint = dim;

    var rule = Box();
    rule.color = 0xFF2A2A31;
    rule.fixedHeight = 2;
    rule.fixedWidth = 200;

    var status = Box();
    status.direction = directionRow;
    status.space = 14;
    status.align = alignCenter;
    status.crossAlign = alignCenter;
    status.kids = [percent(), dot(), gpsLabel()];

    var hint = Text();
    hint.value = 'TAP FOR SECONDS';
    hint.size = 14;
    hint.tint = 0xFF44444E;

    var column = Box();
    column.color = 0xFF000000;
    column.growFactor = 1;
    column.space = 18;
    column.align = alignCenter;
    column.crossAlign = alignCenter;
    column.pad = inset;
    column.kids = [time, date, rule, status, hint];
    column.onTap = () {
      setState(() {
        showSeconds = !showSeconds;
      });
    };

    // The ring sits over everything, inscribed in the full display rather
    // than the safe area — it is meant to hug the bezel.
    var ring = Arc();
    ring.color = palette;
    ring.thickness = 6;
    ring.start = 0;
    ring.sweep = clock.minuteSweep;
    ring.size = uiWidth();

    // A dim full ring underneath, so the gap reads as a track rather than a
    // missing piece.
    var track = Arc();
    track.color = 0xFF1C1C21;
    track.thickness = 6;
    track.sweep = 360;
    track.size = uiWidth();

    var root = Box();
    root.color = 0xFF000000;
    root.growFactor = 1;
    root.kids = [column, track, ring];
    return root;
  }

  Widget percent() {
    var t = Text();
    t.value = '$battery%';
    t.size = 18;
    t.tint = dim;
    return t;
  }

  Widget dot() {
    var d = Box();
    d.color = hasGps ? 0xFF7FD17F : 0xFFE05252;
    d.fixedWidth = 10;
    d.fixedHeight = 10;
    d.corner = 5; // now actually drawn as a circle
    return d;
  }

  Widget gpsLabel() {
    var t = Text();
    t.value = hasGps ? 'GPS' : 'NO GPS';
    t.size = 18;
    t.tint = dim;
    return t;
  }
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
