// One source file can be reached by more than one path — through a symlink,
// or on a case-insensitive filesystem by changing the capitalisation. The
// compiler has to recognise those as the same file, or it loads the
// declarations twice and rejects a valid program for redefining them.
//
//   dart test test/imports_test.dart

import 'dart:io';

import 'package:test/test.dart';

void main() {
  final repoRoot = Directory.current.path.endsWith('tools/mothc')
      ? Directory.current.parent.parent.path
      : Directory.current.path;
  final mothc = '$repoRoot/tools/mothc/bin/mothc.dart';

  late Directory dir;

  setUp(() {
    dir = Directory.systemTemp.createTempSync('moth_imports');
    File('${dir.path}/helper.dart').writeAsStringSync('int shared() => 1;\n');
  });

  tearDown(() => dir.deleteSync(recursive: true));

  /// Compiles [entry] and returns mothc's combined output.
  ProcessResult compile(String entry) => Process.runSync(
      'dart', ['run', mothc, entry, '-o', '${dir.path}/out.mothb']);

  test('a file imported through a symlink is loaded once', () {
    Link('${dir.path}/alias.dart').createSync('${dir.path}/helper.dart');
    File('${dir.path}/via_link.dart').writeAsStringSync(
        "import 'alias.dart';\nint plus() => shared() + 10;\n");
    File('${dir.path}/main.dart')
        .writeAsStringSync("import 'helper.dart';\nimport 'via_link.dart';\n"
            "void main() { print(shared()); print(plus()); }\n");

    final r = compile('${dir.path}/main.dart');
    expect(r.exitCode, 0,
        reason: 'the same file under two paths must not collide:\n'
            '${r.stdout}${r.stderr}');
  });

  test('genuinely duplicated declarations are still rejected', () {
    File('${dir.path}/other.dart').writeAsStringSync('int shared() => 2;\n');
    File('${dir.path}/main.dart')
        .writeAsStringSync("import 'helper.dart';\nimport 'other.dart';\n"
            "void main() { print(shared()); }\n");

    final r = compile('${dir.path}/main.dart');
    expect(r.exitCode, isNot(0));
    expect('${r.stdout}${r.stderr}', contains('already defined'));
  });
}
