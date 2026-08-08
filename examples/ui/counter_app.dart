// A counter that rebuilds on tap, using the widget layer next door.
//
//   dart run tools/mothc/bin/mothc.dart examples/ui/counter_app.dart
//   ./build/mothsim examples/ui/counter_app.mothb --size 466x466 --round --tap 240,240

import 'lib/widgets.dart';

// ---- the app -------------------------------------------------------------

final palette = [0xFF7AA2F7, 0xFFBB9AF7, 0xFF9ECE6A, 0xFFE0AF68, 0xFFF7768E];

class CounterApp extends Component {
  int count = 0;

  Widget build() {
    var readout = Text();
    readout.value = 'count: $count';
    readout.size = 22;

    var hint = Text();
    hint.value = count == 0 ? 'tap the panel' : 'tapped $count times';
    hint.size = 14;
    hint.tint = 0xFF9AA2B8;

    var bar = Box();
    bar.color = palette[count % palette.length];
    bar.fixedHeight = 40;
    bar.corner = 8;

    var body = Box();
    body.color = 0xFF1A1B26;
    body.pad = 18;
    body.space = 12;
    body.corner = 12;
    body.growFactor = 1;
    body.kids = [readout, hint, bar];

    // The whole surface is tappable, and the handler captures `this`.
    // Keep content inside the visible rectangle — on a round panel that is
    // the inscribed square, so the corners are never used.
    var inset = uiSafeArea(0);
    if (inset < 16) inset = 16;

    var surface = Box();
    surface.color = 0xFF16161E;
    surface.pad = inset;
    surface.align = alignCenter;
    surface.growFactor = 1; // fill the display, not just the content
    surface.kids = [body];
    surface.onTap = () {
      setState(() {
        count += 1;
      });
    };
    return surface;
  }
}

/// The deepest node in the tree, to show it survives a rebuild.
int lastBarNode() {
  var el = rootElement;
  while (el != null && el.kids.length > 0) {
    el = el.kids[el.kids.length - 1];
  }
  return el == null ? -1 : el.node;
}

void main() {
  var app = CounterApp();
  runApp(app);
  print('mounted ${mounted.length} elements on ${uiWidth()}x${uiHeight()}, '
      'safe area ${uiSafeArea(2)}x${uiSafeArea(3)} at ${uiSafeArea(0)},${uiSafeArea(1)}');

  var last = millis();
  var reported = -1;
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;

    if (app.count != reported) {
      reported = app.count;
      // Node ids are never reused, so an unchanged id proves the reconciler
      // updated the existing node instead of recreating it.
      print('count $reported: ${mounted.length} elements, '
          'root node ${rootElement!.node}, bar node ${lastBarNode()}');
    }
    delay(16);
  }
}
