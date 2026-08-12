// The interactive basics: a slider driving a brightness ring, a switch
// gating it. Drag the slider and the ring and percentage follow; flip the
// switch and everything dims to show "off".

import 'package:moth/widgets.dart';

class Controls extends Component {
  double level = 0.6;
  bool enabled = true;

  // Closures capture `this`, not locals — so `(v) => setState(() => level =
  // v)` cannot compile: the inner lambda would need the outer one's
  // parameter. The moth idiom is a setter method: assign from the
  // parameter (a lambda may use its own parameters), then setState with an
  // empty closure to mark dirty.
  void setLevel(double v) {
    level = v;
    setState(() {});
  }

  void setEnabled(bool on) {
    enabled = on;
    setState(() {});
  }

  Widget build() {
    var accent = enabled ? 0xFFE8A33D : 0xFF44444E;
    var pct = (level * 100) ~/ 1;

    return Container(
      color: 0xFF0E0E12,
      flex: 1,
      child: Stack(children: [
        Container(
          color: 0xFF0E0E12,
          flex: 1,
          padding: uiSafeArea(0),
          child: Column(
            mainAxisAlignment: mainAxisCenter,
            crossAxisAlignment: crossAxisCenter,
            spacing: 20,
            children: [
              Text('BRIGHTNESS', style: TextStyle(fontSize: 20, color: accent)),
              Text('$pct%', style: TextStyle(fontSize: 72, color: 0xFFF2EFE7)),
              Slider(
                value: level,
                min: 0,
                max: 1,
                width: 220,
                activeColor: accent,
                onChanged: (v) => setLevel(v),
              ),
              Switch(
                value: enabled,
                activeColor: 0xFFE8A33D,
                onChanged: (on) => setEnabled(on),
              ),
            ],
          ),
        ),
        CircularProgressIndicator(
          value: enabled ? level : 0,
          strokeWidth: 8,
          color: accent,
          backgroundColor: 0xFF1C1C21,
          size: uiWidth(),
        ),
      ]),
    );
  }
}

void main() {
  runApp(Controls());
  var last = millis();
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    delay(30);
  }
}
