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

  test('dart: imports say why they cannot work', () {
    final out = compile("import 'dart:io';\nvoid main() { print(1); }\n");
    expect(out, contains('not available on a microcontroller'));
  });
}
