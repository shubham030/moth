// What a frame costs, so R3 is designed against measurements rather than
// guesses. Set MOTH_FRAME_PROFILE to 1 in ui/esp-s3/main/app_main.c, embed
// this, and read the serial log.
//
// Two scenes: a watch-face-shaped one, and a nearly empty one. The second is
// the floor — whatever it costs is what a frame costs before anything is
// drawn, and no amount of drawing less gets below it.

import 'package:moth/widgets.dart';

var tick = 0;
/// Set true to measure the floor alone — the two scenes are timed in
/// separate runs, because one PHASES window straddling the change mixes
/// them and the split stops adding up to the total.
var minimal = false;

class Scene extends Component {
  void bump() => setState(() => tick += 1);

  Widget build() {
    if (minimal) {
      // The floor: one small box on a background.
      return Container(
          color: 0xFF000000,
          flex: 1,
          child: Container(width: 20, height: 20, color: 0xFF3333AA));
    }

    return Container(
      color: 0xFF000000,
      flex: 1,
      child: Stack(children: [
        Container(
          color: 0xFF000000,
          flex: 1,
          padding: uiSafeArea(0),
          child: Column(
            mainAxisAlignment: mainAxisCenter,
            crossAxisAlignment: crossAxisCenter,
            spacing: 18,
            children: [
              Text('14:$tick',
                  style: TextStyle(fontSize: 72, color: 0xFFF2EFE7)),
              Text('FRI 8 AUG', style: TextStyle(fontSize: 20, color: 0xFF6B6B76)),
              Divider(thickness: 2, width: 200),
              Text('TAP FOR SECONDS',
                  style: TextStyle(fontSize: 14, color: 0xFF44444E)),
            ],
          ),
        ),
        CircularProgressIndicator(
            value: 0.55,
            strokeWidth: 6,
            color: 0xFFE8A33D,
            backgroundColor: 0xFF1C1C21,
            size: uiWidth()),
      ]),
    );
  }
}

void run(String label, Scene s, int n) {
  // Warm up, so the first frame's allocations are not in the average.
  for (var i = 0; i < 3; i++) {
    s.bump();
    pumpFrame(16);
  }
  var start = millis();
  for (var i = 0; i < n; i++) {
    s.bump();
    pumpFrame(16);
  }
  var total = millis() - start;
  print('$label: $n repaints in ${total}ms = ${total / n}ms each');
}

void main() {
  var s = Scene();
  runApp(s);

  run(minimal ? 'FLOOR' : 'WATCHFACE', s, 40);

  var last = millis();
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    delay(100);
  }
}
