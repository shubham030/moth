// Keys: reorder a list and watch each row keep its own node.
//
// Node ids are never reused, so printing them shows whether an element
// followed its widget (keyed) or stayed at its position (unkeyed).
//
//   ./build/mothsim examples/ui/keyed_list.mothb --tap 240,60 --tap 240,60

import 'lib/widgets.dart';

final palette = [0xFF7AA2F7, 0xFFBB9AF7, 0xFF9ECE6A, 0xFFE0AF68];

var order = ['alpha', 'beta', 'gamma'];
var useKeys = true;

class ListApp extends Component {
  int rotations = 0;

  Widget build() {
    var rows = [];
    for (var i = 0; i < order.length; i++) {
      var label = Text();
      label.value = order[i];
      label.size = 16;

      var row = Box();
      if (useKeys) row.key = order[i]; // identity follows the item
      row.color = palette[i % palette.length];
      row.pad = 8;
      row.corner = 6;
      row.kids = [label];
      rows.add(row);
    }

    var title = Text();
    title.value = useKeys ? 'keyed, $rotations rotations' : 'unkeyed';
    title.size = 16;

    var list = Box();
    list.space = 8;
    list.kids = rows;
    list.growFactor = 1;

    var screen = Box();
    screen.color = 0xFF16161E;
    screen.pad = 20;
    screen.space = 12;
    screen.growFactor = 1;
    screen.kids = [title, list];
    screen.onTap = () {
      setState(() {
        rotate();
      });
    };
    return screen;
  }

  void rotate() {
    var first = order[0];
    var moved = [];
    for (var i = 1; i < order.length; i++) {
      moved.add(order[i]);
    }
    moved.add(first);
    order = moved;
    rotations += 1;
  }
}

/// Prints which node each row is drawn by, so reordering is visible.
void reportRows(Element root) {
  var listEl = root.kids[0].kids[1];
  var line = '';
  for (var i = 0; i < listEl.kids.length; i++) {
    var rowEl = listEl.kids[i];
    line = '$line ${rowEl.widget.key == '' ? '?' : rowEl.widget.key}=node${rowEl.node}';
  }
  print('order$line');
}

void main() {
  var app = ListApp();
  runApp(app);
  print(useKeys ? 'matching by key' : 'matching by position');
  reportRows(rootElement!);

  var last = millis();
  var seen = 0;
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    if (app.rotations != seen) {
      seen = app.rotations;
      reportRows(rootElement!);
    }
    delay(16);
  }
}
