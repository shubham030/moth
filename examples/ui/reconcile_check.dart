// Exercises the reconciler paths the counter demo never takes. Each check
// prints PASS or FAIL, so a regression is visible in one run:
//
//   ./build/mothsim examples/ui/reconcile_check.mothb --frames 40
//
// The cases are the ones a review found broken: a child changing widget kind,
// elements lingering after unmount, a nested Component losing its setState
// binding when its parent rebuilds, and runApp leaving the old tree attached.
// The fifth finding — one tap firing twice — needs a real tap, so it lives in
// tap_check.dart.

import 'lib/widgets.dart';

var failures = 0;

void check(String what, bool ok) {
  if (!ok) failures += 1;
  print('${ok ? "PASS" : "FAIL"}  $what');
}

// ---- 1: a child that changes kind must be replaced, not just destroyed ----

var showText = true;

class Swapper extends Component {
  Widget build() {
    var child = showText ? textChild() : boxChild();
    var host = Box();
    host.color = 0xFF1A1B26;
    host.pad = 6;
    host.kids = [child];
    return host;
  }

  Widget textChild() {
    var t = Text();
    t.value = 'text';
    return t;
  }

  Widget boxChild() {
    var b = Box();
    b.color = 0xFF9ECE6A;
    b.fixedHeight = 20;
    return b;
  }
}

// ---- 2, 3: shrinking child lists, and Components created during a build ----

var rowCount = 3;

class Inner extends Component {
  int bumps = 0;

  Widget build() {
    var t = Text();
    t.value = 'inner $bumps';
    return t;
  }
}

/// The Component instance the most recent parent build produced.
Inner? liveInner;

class Outer extends Component {
  Widget build() {
    var rows = [];
    for (var i = 0; i < rowCount; i++) {
      var r = Text();
      r.value = 'row $i';
      rows.add(r);
    }
    // A fresh Component each build — the case where the element binding has to
    // be re-established, rather than surviving from the original mount.
    var nested = Inner();
    liveInner = nested;
    rows.add(nested);

    var host = Box();
    host.color = 0xFF16161E;
    host.kids = rows;
    return host;
  }

  void again() {
    setState(() {});
  }
}

void main() {
  // --- case 1: a child changing widget kind ---
  var swapper = Swapper();
  runApp(swapper);
  var before = mounted.length;
  var beforeNode = rootElement!.kids[0].kids[0].node;

  showText = false;
  swapper.setState(() {});
  pumpFrame(16);

  var childEl = rootElement!.kids[0].kids[0];
  check('child adopts the new widget kind', childEl.widget.typeName() == 'Box');
  // The old node was destroyed, so a correct replacement has a fresh id — ids
  // are never reused. Keeping the old id means nothing was rebuilt.
  check('a kind change produces a new live node', childEl.node != beforeNode);
  check('element count steady after a swap', mounted.length == before);

  // --- case 2: children dropped from a rebuild must leave the live set ---
  var outer = Outer();
  runApp(outer);
  var withThree = mounted.length;
  rowCount = 1;
  outer.again();
  pumpFrame(16);
  check('unmounted elements leave the live set', mounted.length < withThree);

  // --- case 3: a Component built by its parent must be bound to its element ---
  outer.again(); // the rebuild replaces the nested Component with a new one
  pumpFrame(16);
  check('nested component is bound after a parent rebuild',
      liveInner!.element != null);

  // --- case 4: a second runApp replaces the first tree, it does not stack ---
  var fresh = Swapper();
  runApp(fresh);
  check('runApp replaces the previous tree',
      rootElement!.widget == fresh && mounted.length == before);

  print(failures == 0 ? 'all checks passed' : '$failures check(s) failed');

  var last = millis();
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    delay(16);
  }
}
