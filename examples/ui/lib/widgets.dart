// A minimal Flutter-shaped widget layer over the moth display bindings.
//
// Widgets are immutable descriptions; Elements are retained and own a
// renderer node. setState marks an element dirty, and the next frame rebuilds
// just that subtree and diffs the result into ui* calls.
//
// This is the beginning of package:moth — for now it is a plain file that
// programs import.

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
  /// Optional identity. When set, the reconciler matches children by key
  /// instead of by position, so a reordered child keeps its element — and
  /// therefore its node — rather than having a sibling's state applied to it.
  String key = '';

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
    var keyed = false;
    for (final w in nextChildren) {
      if (w.key != '') keyed = true;
    }
    if (keyed) {
      reconcileKeyed(nextChildren);
      return;
    }

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

  /// Match by key, so moving a child moves its element rather than pouring a
  /// different widget's state into whatever element sat at that position.
  void reconcileKeyed(List nextChildren) {
    var previous = kids;
    kids = [];

    for (final w in nextChildren) {
      var reused = findByKey(previous, w.key);
      if (reused == null) {
        var child = Element(w);
        child.parent = this;
        child.mount(node);
        kids.add(child);
      } else {
        reused.update(w);
        kids.add(reused);
      }
    }

    // Anything left behind is genuinely gone.
    for (final leftover in previous) {
      if (leftover != null) leftover.unmount();
    }

    // Put the renderer's children back in the order the widgets describe.
    for (var i = 0; i < kids.length; i++) {
      uiAttach(node, kids[i].node, i);
    }
  }

  /// Takes the match out of [pool] so one element is never reused twice.
  Element? findByKey(List pool, String key) {
    if (key == '') return null;
    for (var i = 0; i < pool.length; i++) {
      var candidate = pool[i];
      if (candidate != null && candidate.widget.key == key) {
        pool[i] = null;
        return candidate;
      }
    }
    return null;
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

