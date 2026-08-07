// Callbacks, not a polling switch: each view owns its handler, and tapping
// it runs that handler. This is the shape the widget layer is built from.
//
//   dart run tools/mothc/bin/mothc.dart examples/ui/callbacks.dart
//   ./build/mothsim examples/ui/callbacks.mothb --tap 120,250 --tap 300,250

final kBox = 0;
final kLabel = 1;

final propHeight = 1;
final propDirection = 2;
final propGrow = 5;
final propGap = 6;
final propPadding = 7;
final propBgColor = 11;
final propRadius = 12;
final propOpacity = 15;
final propText = 16;
final propFontSize = 17;
final propTextColor = 18;

final directionRow = 1;
final easeInOut = 2;
final eventClicked = 2;

/// Every view that can receive events. The renderer reports a node id, so
/// something has to map ids back to objects; dispatch happens in Dart
/// because a native cannot re-enter the VM.
var views = [];

class Box {
  int id;
  Function? handler;

  Box(this.id);

  void add(Box child) => uiAttach(id, child.id, -1);
  void background(int argb) => uiSetInt(id, propBgColor, argb);
  void height(int px) => uiSetNum(id, propHeight, px);
  void grow(int factor) => uiSetNum(id, propGrow, factor);
  void padding(int px) => uiSetNum(id, propPadding, px);
  void gap(int px) => uiSetNum(id, propGap, px);
  void radius(int px) => uiSetNum(id, propRadius, px);
  void row() => uiSetInt(id, propDirection, directionRow);

  void fade(int fromPercent, int toPercent, int ms) {
    uiAnimate(id, propOpacity, fromPercent / 100, toPercent / 100, ms, easeInOut);
  }

  /// Hand the view a function to run when tapped — it keeps it itself.
  void onTap(Function fn) {
    handler = fn;
    views.add(this);
  }

  void fireTap() {
    if (handler != null) handler!();
  }
}

class Label {
  int id;
  Label(this.id);
  void text(String value) => uiSetText(id, propText, value);
  void color(int argb) => uiSetInt(id, propTextColor, argb);
  void fontSize(int px) => uiSetNum(id, propFontSize, px);
}

Box rootBox() => Box(uiRoot());
Box newBox() => Box(uiCreate(kBox));
Label newLabel() => Label(uiCreate(kLabel));

void dispatchEvents() {
  var packed = uiPoll();
  while (packed >= 0) {
    var node = packed ~/ 8;
    var kind = packed % 8;
    if (kind == eventClicked) {
      for (final view in views) {
        if (view.id == node) view.fireTap();
      }
    }
    packed = uiPoll();
  }
}

// ---- the app -------------------------------------------------------------

final palette = [0xFF7AA2F7, 0xFFBB9AF7, 0xFF9ECE6A, 0xFFE0AF68, 0xFFF7768E];

var readout = Label(0);
var pulse = Box(0);
var taps = 0;

/// Holds its own index as a field, so the closure it registers only needs to
/// capture `this` — which is what moth supports today.
class Card {
  Box box;
  int index;

  Card(this.box, this.index);

  void wire() {
    box.onTap(() {
      tapped();
    });
  }

  void tapped() {
    taps += 1;
    box.background(palette[(taps + index) % palette.length]);
    readout.text('card ${index + 1}, tap $taps');
    print('card ${index + 1}, tap $taps');
    pulse.fade(100, 20, 600);
  }
}

void main() {
  var root = rootBox();
  root.background(0xFF16161E);
  root.padding(18);
  root.gap(14);

  readout = newLabel();
  readout.text('tap a card');
  readout.fontSize(20);
  readout.color(0xFFC0CAF5);
  uiAttach(root.id, readout.id, -1);

  var row = newBox();
  row.row();
  row.gap(14);
  row.grow(1);
  root.add(row);

  for (var i = 0; i < 3; i++) {
    var box = newBox();
    box.background(palette[i]);
    box.grow(i + 1);
    box.radius(10);
    row.add(box);
    Card(box, i).wire();
  }

  pulse = newBox();
  pulse.background(0xFFF7768E);
  pulse.height(22);
  pulse.radius(6);
  root.add(pulse);
  pulse.fade(100, 20, 900);

  print('display ${uiWidth()}x${uiHeight()} — ${views.length} tappable views');

  var last = millis();
  while (true) {
    var now = millis();
    uiTick(now - last);
    last = now;

    dispatchEvents();
    uiCommit();
    delay(16);
  }
}
