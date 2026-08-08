// A tap must fire exactly one handler, even when it lands on a node a
// composite adopted. Two elements refer to that node — the composite and the
// child that owns it — so a dispatcher that walks every match bubbles twice
// and the handler above them runs twice for one finger.
//
//   ./build/mothsim examples/ui/tap_check.mothb --frames 80 --tap 100,30

import 'package:moth/widgets.dart';

var taps = 0;

class Passive extends Component {
  Widget build() {
    var b = Box();
    b.color = 0xFF7AA2F7;
    b.fixedHeight = 60;
    return b;
  }
}

class TapRoot extends Component {
  Widget build() {
    var outer = Box();
    outer.color = 0xFF16161E;
    outer.pad = 10;
    outer.growFactor = 1;
    // The handler sits here; the tap lands on the composite's adopted node,
    // which is a descendant. Bubbling has to reach this once, not twice.
    outer.kids = [Passive()];
    outer.onTap = () {
      taps += 1;
    };
    return outer;
  }
}

void main() {
  runApp(TapRoot());
  print('ready — tap inside the blue band');

  var last = millis();
  var reported = false;
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;

    if (taps > 0 && !reported) {
      reported = true;
      print('${taps == 1 ? "PASS" : "FAIL"}  one tap fires one handler '
          '(fired $taps time(s))');
    }
    delay(16);
  }
}
