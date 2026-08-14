// Things moth refuses to compile.
//
// A rejection is a feature here. The alternative to refusing `async` is
// compiling it as though the keyword were absent, which makes a function that
// returns Future<int> in Dart return a plain int in moth — a program that runs
// and quietly disagrees with the language it is written in. Every case below
// was, at some point, accepted.

import 'dart:io';

import 'package:test/test.dart';

void main() {
  final repoRoot = Directory.current.path.endsWith('tools/mothc')
      ? Directory.current.parent.parent.path
      : Directory.current.path;
  final mothc = '$repoRoot/tools/mothc/bin/mothc.dart';

  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('moth_reject'));
  tearDown(() => dir.deleteSync(recursive: true));

  /// Compiles [source] and returns what mothc said.
  String compile(String source) {
    File('${dir.path}/main.dart').writeAsStringSync(source);
    final r = Process.runSync('dart',
        ['run', mothc, '${dir.path}/main.dart', '-o', '${dir.path}/out.mothb']);
    expect(r.exitCode, isNot(0), reason: 'expected a rejection, got a blob');
    return '${r.stdout}${r.stderr}';
  }

  test('an async function is refused, not silently made synchronous', () {
    final out = compile('''
Future<int> twice(int n) async {
  return n * 2;
}

void main() {
  print(twice(21));
}
''');
    expect(out, contains('async functions are not supported'));
    expect(out, contains('event loop'));
  });

  test('an async main is refused too', () {
    final out = compile('void main() async { print(1); }\n');
    expect(out, contains('async functions are not supported'));
  });

  test('a generator is refused', () {
    final out = compile('''
Iterable<int> counted() sync* {
  yield 1;
}

void main() {
  print(1);
}
''');
    expect(out, contains('generator functions'));
  });

  test('await is refused', () {
    final out = compile('void main() { var v = await 1; print(v); }\n');
    expect(out, isNotEmpty);
  });

  test('a method tear-off traps instead of silently calling the method', () {
    // Reading `obj.method` used to invoke it — side effects and all — because
    // a getter and a zero-argument method look identical by arity. The
    // compiler cannot tell what `c` is, so this is caught at run time, and
    // trapping is the honest answer until tear-offs exist.
    final source = '''
class Counter {
  int n = 0;
  void bump() { n += 1; }
}

void main() {
  var c = Counter();
  var f = c.bump;
  print(c.n);
  print(f);
}
''';
    File('${dir.path}/main.dart').writeAsStringSync(source);
    final blob = '${dir.path}/out.mothb';
    final built = Process.runSync(
        'dart', ['run', mothc, '${dir.path}/main.dart', '-o', blob]);
    expect(built.exitCode, 0, reason: 'the compiler cannot know the receiver');

    final mothrun = '$repoRoot/build/vm/mothrun';
    final ran = Process.runSync(mothrun, [blob]);
    final out = '${ran.stdout}${ran.stderr}';
    expect(out, contains('no field or getter'));
    expect(out, isNot(contains('\n1')), reason: 'bump() must not have run');
  });

  test('a field and an accessor cannot share a name', () {
    // The VM resolves fields first, so the accessor would never run — in a
    // hardware API that is the line that drives the pin.
    final out = compile('''
class Servo {
  int angle = 0;
  set angle(int v) { angle = v; }
}

void main() { print(1); }
''');
    expect(out, contains('already a field'));
  });

  test('a cascade cannot reach through another property', () {
    // Assigning to the wrong object silently is worse than refusing.
    final out = compile('''
class C { var log = []; }
void main() { var c = C()..log.add(1); print(c.log); }
''');
    expect(out, contains('cannot call through another property'));
  });

  test('dart: imports say why they cannot work', () {
    final out = compile("import 'dart:io';\nvoid main() { print(1); }\n");
    expect(out, contains('not available on a microcontroller'));
  });

  test('an external method is refused — only top-level built-ins are', () {
    // Accepted, it compiled to a method whose declared int quietly
    // evaluated to null — the analyzer/compiler agreement broken from the
    // other side.
    for (final member in [
      'external int foo(int a);',
      'external int get x;',
      'external set x(int v);',
      'external int x;',
      'external A();',
    ]) {
      final out = compile('''
class A {
  $member
}
void main() { print(A()); }
''');
      expect(out, contains('cannot be external'),
          reason: 'a class with "$member" must be refused');
    }
  });

  test('a closure capturing an enclosing local is refused, with the idiom', () {
    // The docs' headline constraint — closures capture only `this` — rests
    // on this rejection. Silently compiling it would read the wrong
    // variable at runtime; the hint teaches the field/top-level idiom the
    // examples use.
    final out = compile('''
void main() {
  var x = 1;
  var f = () { print(x); };
  f();
}
''');
    expect(out, contains("cannot use 'x' from the enclosing function"));
    expect(out, contains('field'));
  });
}
