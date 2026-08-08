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
final kArc = 5;

final propWidth = 0;
final propHeight = 1;
final propDirection = 2;
final propMainAlign = 3;
final propCrossAlign = 4;
final propGrow = 5;
final propGap = 6;
final propPadding = 7;
final propPosition = 8;
final propLeft = 9;
final propTop = 10;
final propBgColor = 11;
final propRadius = 12;
final propBorderWidth = 13;
final propBorderColor = 14;
final propText = 16;
final propFontSize = 17;
final propTextColor = 18;
final propArcStart = 23;
final propArcSweep = 24;
final propThickness = 25;

final positionAbsolute = 1;

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
  int fixedWidth = -1;
  int align = 0;

  /// Alignment across the box's other axis — for a column, that is horizontal.
  int crossAlign = 0;
  int borderWidth = 0;
  int borderColor = 0;
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
    uiSetInt(node, propCrossAlign, crossAlign);
    uiSetNum(node, propBorderWidth, borderWidth);
    uiSetInt(node, propBorderColor, borderColor);
    if (fixedHeight >= 0) uiSetNum(node, propHeight, fixedHeight);
    if (fixedWidth >= 0) uiSetNum(node, propWidth, fixedWidth);
  }
}

/// A stroked ring segment — a progress ring, a gauge, a dial.
///
/// Angles are degrees clockwise from twelve o'clock, which is how you would
/// describe a progress ring out loud. A [sweep] of 360 or more closes it.
/// The arc is inscribed in its own box, so it is normally placed absolutely
/// over the whole display rather than laid out in the flow.
class Arc extends Widget {
  int color = 0xFFE0AF68;
  int thickness = 6;

  /// Where the stroke begins, and how far round it goes.
  int start = 0;
  int sweep = 360;

  /// Size of the box the ring is inscribed in.
  int size = 0;

  /// Offset from the parent's content box, since an arc is positioned rather
  /// than laid out.
  int left = 0;
  int top = 0;

  String typeName() => 'Arc';
  int kind() => kArc;

  void apply(int node) {
    uiSetInt(node, propPosition, positionAbsolute);
    uiSetNum(node, propLeft, left);
    uiSetNum(node, propTop, top);
    if (size > 0) {
      uiSetNum(node, propWidth, size);
      uiSetNum(node, propHeight, size);
    }
    uiSetInt(node, propBgColor, color);
    uiSetNum(node, propThickness, thickness);
    uiSetNum(node, propArcStart, start);
    uiSetNum(node, propArcSweep, sweep);
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

/// Every live element. A composite adopts its child's node rather than owning
/// one, so several elements can report the same node id — only the owner is a
/// hit-test target.
var mounted = [];
var dirtyElements = [];

/// Forgets an element that is going away, so a destroyed node can never be
/// dispatched to and a dirty element cannot be rebuilt after unmounting.
void forgetElement(Element gone) {
  var keptMounted = [];
  for (final e in mounted) {
    if (e != gone) keptMounted.add(e);
  }
  mounted = keptMounted;

  var keptDirty = [];
  for (final e in dirtyElements) {
    if (e != gone) keptDirty.add(e);
  }
  dirtyElements = keptDirty;
}

class Element {
  Widget widget;
  int node = 0;
  var kids = [];
  bool isDirty = false;
  Element? parent;

  /// The renderer node this element's node was attached to, and its position
  /// among that node's children. Kept so a replacement can take the same
  /// place rather than being appended at the end.
  int hostNode = 0;
  int slotIndex = -1;

  /// False for composites, which borrow their child's node. Exactly one
  /// element owns any given node.
  bool ownsNode = false;

  Element(this.widget);

  void markDirty() {
    if (isDirty) return;
    isDirty = true;
    dirtyElements.add(this);
  }

  void mount(int parentNode, int index) {
    hostNode = parentNode;
    slotIndex = index;

    if (widget.composite()) {
      // A composite has no node of its own; it adopts its child's.
      widget.attachElement(this);
      var child = Element(widget.build());
      child.parent = this;
      child.mount(parentNode, index);
      kids = [child];
      node = child.node;
      mounted.add(this);
      return;
    }

    node = uiCreate(widget.kind());
    ownsNode = true;
    widget.apply(node);
    uiAttach(parentNode, node, index);
    mounted.add(this);

    // Mounted with their real slot rather than -1 ("append"). The order is the
    // same either way, but a child that later changes kind reads slotIndex to
    // find its place, and -1 would send the replacement to the end.
    var slot = 0;
    for (final childWidget in widget.children()) {
      var child = Element(childWidget);
      child.parent = this;
      child.mount(node, slot);
      kids.add(child);
      slot += 1;
    }
  }

  /// Reconcile against a new description of the same kind of thing. Callers
  /// go through updateOrReplace, which handles a change of kind.
  void update(Widget next) {
    widget = next;
    if (widget.composite()) {
      // The rebuilt parent handed us a fresh widget object; it needs to know
      // which element it belongs to or its setState would go nowhere.
      widget.attachElement(this);
      rebuild();
      return;
    }

    widget.apply(node); // property diffing happens in the renderer
    reconcileChildren(widget.children());
  }

  /// Returns the element to use afterwards: this one when the widget kind is
  /// unchanged, a freshly mounted one when it is not.
  Element updateOrReplace(Widget next) {
    if (next.typeName() == widget.typeName()) {
      update(next);
      return this;
    }
    var host = hostNode;
    var index = slotIndex;
    var owner = parent;
    unmount();

    var fresh = Element(next);
    fresh.parent = owner;
    fresh.mount(host, index);
    return fresh;
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
      kids[i] = kids[i].updateOrReplace(nextChildren[i]);
    }
    for (var i = shared; i < nextChildren.length; i++) {
      var child = Element(nextChildren[i]);
      child.parent = this;
      child.mount(node, i);
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

    // Unkeyed children in a keyed list still match by position, against the
    // previous unkeyed children. Without this a single keyed sibling would
    // remount every unkeyed one on each rebuild, losing their state.
    var scan = 0;

    for (var i = 0; i < nextChildren.length; i++) {
      var w = nextChildren[i];
      Element? reused;
      if (w.key != '') {
        reused = findByKey(previous, w.key);
      } else {
        while (scan < previous.length) {
          var candidate = previous[scan];
          scan += 1;
          if (candidate != null && candidate.widget.key == '') {
            previous[scan - 1] = null; // claimed
            reused = candidate;
            break;
          }
        }
      }
      if (reused == null) {
        var child = Element(w);
        child.parent = this;
        child.mount(node, i);
        kids.add(child);
      } else {
        kids.add(reused.updateOrReplace(w));
      }
    }

    // Anything left behind is genuinely gone.
    for (final leftover in previous) {
      if (leftover != null) leftover.unmount();
    }

    // Put the renderer's children back in the order the widgets describe.
    for (var i = 0; i < kids.length; i++) {
      kids[i].slotIndex = i;
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
      if (k != null) k.unmount();
    }
    kids = [];
    forgetElement(this);
    // A composite's node belongs to its child, which has just destroyed it.
    if (ownsNode) uiDestroy(node);
  }

  /// A composite rebuilds by asking its widget to describe itself again.
  void rebuild() {
    isDirty = false;
    var next = widget.build();
    if (kids.length == 0) return;
    kids[0] = kids[0].updateOrReplace(next);
    node = kids[0].node;

    // A composite borrows its child's node. If that node was just replaced,
    // every ancestor that borrowed ours is now naming a destroyed node — and
    // uiAttach on a destroyed id fails silently, so a later keyed reorder
    // would quietly skip this subtree.
    var borrowed = this;
    var up = parent;
    while (up != null &&
        !up.ownsNode &&
        up.kids.length > 0 &&
        up.kids[0] == borrowed) {
      up.node = node;
      borrowed = up;
      up = up.parent;
    }
  }

  Function? tapHandler() => widget.tapHandler();
}

Element? rootElement;

void runApp(Widget app) {
  // Replace whatever was running. Without this a second runApp leaves the
  // first tree attached, so both draw and hit-testing finds the older one.
  if (rootElement != null) rootElement!.unmount();
  rootElement = Element(app);
  rootElement!.mount(uiRoot(), -1);
}

/// One pass of the framework: drain events, rebuild dirty subtrees, commit.
void pumpFrame(int dtMs) {
  uiTick(dtMs);

  var packed = uiPoll();
  while (packed >= 0) {
    var nodeId = packed ~/ 8;
    var kind = packed % 8;
    if (kind == eventClicked) {
      // Exactly one element owns a node; composites share their child's id
      // and would otherwise make a single tap fire more than once.
      Element? hit;
      for (final el in mounted) {
        if (el.ownsNode && el.node == nodeId) hit = el;
      }
      // Hit-testing reports the innermost node; the handler may be on an
      // ancestor, so the event bubbles up until one takes it — as in Flutter.
      var target = hit;
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
    packed = uiPoll();
  }

  // A build() that calls setState re-dirties itself, so an unbounded drain
  // would never return and the frame would simply stop. Bounding it turns
  // that mistake into a message instead of a hang.
  var passes = 0;
  while (dirtyElements.length > 0) {
    passes += 1;
    if (passes > 100) {
      print(
        'moth: setState appears to be called from build() — '
        'giving up on this frame',
      );
      dirtyElements = [];
      break;
    }
    var el = dirtyElements.removeLast();
    el.rebuild();
  }

  uiCommit();
}
