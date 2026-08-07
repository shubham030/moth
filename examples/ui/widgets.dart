// M3: widgets and setState.
//
// Widgets are immutable descriptions; Elements are retained and own a
// renderer node. setState marks an element dirty, and the next tick rebuilds
// just that subtree and diffs the result into ui* calls.
//
//   dart run tools/mothc/bin/mothc.dart examples/ui/widgets.dart
//   ./build/mothsim examples/ui/widgets.mothb --tap 240,150 --tap 240,150

// ---- contract numbering --------------------------------------------------

final kBox = 0;
final kLabel = 1;

final propHeight = 1;
final propDirection = 2;
final propMainAlign = 3;
final propGrow = 5;
final propGap = 6;
final propPadding = 7;
final propBgColor = 11;
final propRadius = 12;
final propText = 16;
final propFontSize = 17;
final propTextColor = 18;

final directionRow = 1;
final alignCenter = 1;
final eventClicked = 2;

// ---- widgets -------------------------------------------------------------

/// An immutable description of a piece of UI. Cheap to build and throw away.
class Widget {
  String typeName() => 'Widget';
  int kind() => kBox;
  List children() => [];
  void apply(int node) {}

  /// Composite widgets describe themselves in terms of other widgets.
  bool composite() => false;
  Widget build() => this;

  /// Overridden by stateful widgets so they can find their element again.
  /// A virtual no-op avoids needing an `is` test here.
  void attachElement(Element e) {}

  Function? tapHandler() => null;
}

class Box extends Widget {
  int color = 0;
  int pad = 0;
  int space = 0;
  int direction = 0;
  int growFactor = 0;
  int corner = 0;
  int fixedHeight = -1;
  int align = 0;
  var kids = [];
  Function? onTap;

  String typeName() => 'Box';
  int kind() => kBox;
  List children() => kids;
  Function? tapHandler() => onTap;

  void apply(int node) {
    uiSetInt(node, propBgColor, color);
    uiSetNum(node, propPadding, pad);
    uiSetNum(node, propGap, space);
    uiSetInt(node, propDirection, direction);
    uiSetNum(node, propGrow, growFactor);
    uiSetNum(node, propRadius, corner);
    uiSetInt(node, propMainAlign, align);
    if (fixedHeight >= 0) uiSetNum(node, propHeight, fixedHeight);
  }
}

class Text extends Widget {
  String value = '';
  int size = 16;
  int tint = 0xFFC0CAF5;

  String typeName() => 'Text';
  int kind() => kLabel;

  void apply(int node) {
    uiSetText(node, propText, value);
    uiSetNum(node, propFontSize, size);
    uiSetInt(node, propTextColor, tint);
  }
}

/// A widget that holds state and rebuilds itself. State lives on the widget
/// rather than a separate State object, which keeps it to one class without
/// generics.
class Component extends Widget {
  Element? element;

  bool composite() => true;
  Widget build() => Text();

  void attachElement(Element e) {
    element = e;
  }

  /// Change state, then ask the framework to rebuild this subtree.
  void setState(Function change) {
    change();
    if (element != null) element!.markDirty();
  }
}

// ---- elements ------------------------------------------------------------

/// Every element that owns a renderer node, so a tap on a node id can be
/// routed back to the widget that produced it.
var mounted = [];
var dirtyElements = [];

class Element {
  Widget widget;
  int node = 0;
  var kids = [];
  bool isDirty = false;
  Element? parent;

  Element(this.widget);

  void markDirty() {
    if (isDirty) return;
    isDirty = true;
    dirtyElements.add(this);
  }

  void mount(int parentNode) {
    if (widget.composite()) {
      // A composite has no node of its own; it adopts its child's.
      widget.attachElement(this);
      var child = Element(widget.build());
      child.parent = this;
      child.mount(parentNode);
      kids = [child];
      node = child.node;
      mounted.add(this);
      return;
    }

    node = uiCreate(widget.kind());
    widget.apply(node);
    uiAttach(parentNode, node, -1);
    mounted.add(this);

    for (final childWidget in widget.children()) {
      var child = Element(childWidget);
      child.parent = this;
      child.mount(node);
      kids.add(child);
    }
  }

  /// Reconcile against a new description of the same thing.
  void update(Widget next) {
    if (next.typeName() != widget.typeName()) {
      // Different kind of widget: replace the node entirely.
      var parentless = node;
      widget = next;
      uiDestroy(parentless);
      return;
    }

    widget = next;
    if (widget.composite()) {
      rebuild();
      return;
    }

    widget.apply(node); // property diffing happens in the renderer
    reconcileChildren(widget.children());
  }

  void reconcileChildren(List nextChildren) {
    var shared = kids.length;
    if (nextChildren.length < shared) shared = nextChildren.length;

    for (var i = 0; i < shared; i++) {
      kids[i].update(nextChildren[i]);
    }
    for (var i = shared; i < nextChildren.length; i++) {
      var child = Element(nextChildren[i]);
      child.parent = this;
      child.mount(node);
      kids.add(child);
    }
    while (kids.length > nextChildren.length) {
      var gone = kids.removeLast();
      gone.unmount();
    }
  }

  void unmount() {
    for (final k in kids) {
      k.unmount();
    }
    kids = [];
    uiDestroy(node);
  }

  /// A composite rebuilds by asking its widget to describe itself again.
  void rebuild() {
    isDirty = false;
    var next = widget.build();
    if (kids.length == 0) return;
    kids[0].update(next);
    node = kids[0].node;
  }

  Function? tapHandler() => widget.tapHandler();
}

Element? rootElement;

void runApp(Widget app) {
  rootElement = Element(app);
  rootElement!.mount(uiRoot());
}

/// One pass of the framework: drain events, rebuild dirty subtrees, commit.
void pumpFrame(int dtMs) {
  uiTick(dtMs);

  var packed = uiPoll();
  while (packed >= 0) {
    var nodeId = packed ~/ 8;
    var kind = packed % 8;
    if (kind == eventClicked) {
      // Hit-testing reports the innermost node; the handler may be on an
      // ancestor, so the event bubbles up until one takes it — as in Flutter.
      for (final el in mounted) {
        if (el.node == nodeId) {
          var target = el;
          while (target != null) {
            var handler = target.tapHandler();
            if (handler != null) {
              handler();
              target = null;
            } else {
              target = target.parent;
            }
          }
        }
      }
    }
    packed = uiPoll();
  }

  while (dirtyElements.length > 0) {
    var el = dirtyElements.removeLast();
    el.rebuild();
  }

  uiCommit();
}

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
    var surface = Box();
    surface.color = 0xFF16161E;
    surface.pad = 16;
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
  print('mounted ${mounted.length} elements on ${uiWidth()}x${uiHeight()}');

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
