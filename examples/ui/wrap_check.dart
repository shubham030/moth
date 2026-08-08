// Text wrapping, checked rather than eyeballed.
//
//   ./build/mothsim examples/ui/wrap_check.mothb --frames 30 --shot /tmp/wrap.ppm
//
// A label given a wrapWidth breaks at spaces and grows downwards; the box it
// reports has to match the lines actually drawn, or text spills out of its own
// layout. Height is measured through the element's node, so these assert on
// what the renderer decided, not on what this program hoped for.

import 'package:moth/widgets.dart';

var failures = 0;

void check(String what, bool ok) {
  if (!ok) failures += 1;
  print('${ok ? "PASS" : "FAIL"}  $what');
}

class Wrapped extends Component {
  Widget build() {
    return Box()
      ..color = 0xFF16161E
      ..pad = 10
      ..space = 10
      ..kids = [
        Text()
          ..value = 'one'
          ..size = 20,
        Text()
          ..value = 'the quick brown fox jumps over the lazy dog'
          ..size = 20
          ..wrapWidth = 200,
        Text()
          ..value = 'supercalifragilisticexpialidocious'
          ..size = 20
          ..wrapWidth = 120,
        Text()
          ..value = 'first line\nsecond line'
          ..size = 20,
      ];
  }
}

void main() {
  var app = Wrapped();
  runApp(app);
  pumpFrame(16);

  var kids = rootElement!.kids[0].kids;
  var single = uiFrameOf(kids[0].node, 3);
  var wrapped = uiFrameOf(kids[1].node, 3);
  var longWord = uiFrameOf(kids[2].node, 3);
  var explicit = uiFrameOf(kids[3].node, 3);

  print('heights: single=$single wrapped=$wrapped longWord=$longWord explicit=$explicit');

  check('a short label stays one line', single > 0);
  check('a wrapped label is taller than one line', wrapped > single);
  check('wrapping respects the width',
      uiFrameOf(kids[1].node, 2) <= 200);
  check('a word wider than the box still breaks', longWord > single);
  check('an explicit newline breaks', explicit > single);
  check('wrapped height is a whole number of lines',
      wrapped % single == 0);

  print(failures == 0 ? 'all checks passed' : '$failures check(s) failed');

  var last = millis();
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    delay(16);
  }
}
