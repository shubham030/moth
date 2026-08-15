// The first program where Dart drives pixels: a tappable UI running on the
// moth VM, drawn by moth_render.
//
//   dart run tools/mothc/bin/mothc.dart examples/ui/counter.dart
//   ./build/mothsim examples/ui/counter.mothb
//
// The classes below are a self-contained copy of package:moth's core —
// kept inline so this file also exercises the compiler without imports.
// For the normal way, see counter_app.dart, which imports package:moth.

// ---- the contract's numbering, wrapped so nobody has to remember it ------

final kBox = 0;
final kLabel = 1;

final propWidth = 0;
final propHeight = 1;
final propDirection = 2;
final propMainAlign = 3;
final propCrossAlign = 4;
final propGrow = 5;
final propGap = 6;
final propPadding = 7;
final propBgColor = 11;
final propRadius = 12;
final propOpacity = 15;
final propText = 16;
final propFontSize = 17;
final propTextColor = 18;

final alignStart = 0;
final alignCenter = 1;
final directionRow = 1;
final easeInOut = 2;

final eventClicked = 2;

// ---- views ---------------------------------------------------------------

class Box {
  int id;

  Box(this.id);

  void add(Box child) {
    uiAttach(id, child.id, -1);
  }

  void addLabel(Label child) {
    uiAttach(id, child.id, -1);
  }

  void background(int argb) {
    uiSetInt(id, propBgColor, argb);
  }

  void size(int w, int h) {
    uiSetNum(id, propWidth, w);
    uiSetNum(id, propHeight, h);
  }

  void height(int h) {
    uiSetNum(id, propHeight, h);
  }

  void grow(int factor) {
    uiSetNum(id, propGrow, factor);
  }

  void padding(int px) {
    uiSetNum(id, propPadding, px);
  }

  void gap(int px) {
    uiSetNum(id, propGap, px);
  }

  void row() {
    uiSetInt(id, propDirection, directionRow);
  }

  void radius(int px) {
    uiSetNum(id, propRadius, px);
  }

  void center() {
    uiSetInt(id, propMainAlign, alignCenter);
  }

  void fade(int fromPercent, int toPercent, int ms) {
    uiAnimate(id, propOpacity, fromPercent / 100, toPercent / 100, ms, easeInOut);
  }
}

class Label {
  int id;

  Label(this.id);

  void text(String value) {
    uiSetText(id, propText, value);
  }

  void color(int argb) {
    uiSetInt(id, propTextColor, argb);
  }

  void fontSize(int px) {
    uiSetNum(id, propFontSize, px);
  }
}

Box rootBox() => Box(uiRoot());
Box newBox() => Box(uiCreate(kBox));
Label newLabel() => Label(uiCreate(kLabel));

// ---- the program ---------------------------------------------------------

final palette = [0xFF7AA2F7, 0xFFBB9AF7, 0xFF9ECE6A, 0xFFE0AF68, 0xFFF7768E];

var cards = [];
var readout = Label(0);
var pulse = Box(0);
var taps = 0;

void buildUi() {
  var root = rootBox();
  root.background(0xFF16161E);
  root.padding(18);
  root.gap(14);

  readout = newLabel();
  readout.text('tap a card');
  readout.fontSize(20);
  readout.color(0xFFC0CAF5);
  root.addLabel(readout);

  var row = newBox();
  row.row();
  row.gap(14);
  row.grow(1);
  root.add(row);

  for (var i = 0; i < 3; i++) {
    var card = newBox();
    card.background(palette[i]);
    card.grow(i + 1);
    card.radius(10);
    row.add(card);
    cards.add(card);
  }

  pulse = newBox();
  pulse.background(0xFFF7768E);
  pulse.height(22);
  pulse.radius(6);
  root.add(pulse);
  pulse.fade(100, 20, 900);
}

/// A tapped card takes the next colour and nudges the others along.
void onCardTapped(int index) {
  taps += 1;
  for (var i = 0; i < cards.length; i++) {
    var shade = (taps + i + index) % palette.length;
    cards[i].background(palette[shade]);
  }
  readout.text('tap $taps on card ${index + 1}');
  print('tap $taps on card ${index + 1}');
  pulse.fade(100, 20, 600);
}

int cardIndexFor(int node) {
  for (var i = 0; i < cards.length; i++) {
    if (cards[i].id == node) return i;
  }
  return -1;
}

void main() {
  buildUi();
  print('display ${uiWidth()}x${uiHeight()}');

  var last = millis();
  while (true) {
    var now = millis();
    uiTick(now - last);
    last = now;

    var packed = uiPoll();
    while (packed >= 0) {
      var node = packed ~/ 8;
      var kind = packed % 8;
      if (kind == eventClicked) {
        var index = cardIndexFor(node);
        if (index >= 0) onCardTapped(index);
      }
      packed = uiPoll();
    }

    uiCommit();
    delay(16);
  }
}
