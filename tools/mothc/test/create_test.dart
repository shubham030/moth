// The scaffold's one hard promise: what `mothc create` writes COMPILES.
// A starter that greets a beginner with a compile error is worse than no
// starter, and the widget API the template leans on is still settling —
// this is the tripwire that keeps the two in step.
import 'dart:io';

import 'package:mothc/src/compiler.dart';
import 'package:mothc/src/create.dart';
import 'package:test/test.dart';

void main() {
  late Directory tmp;
  setUp(() => tmp = Directory.systemTemp.createTempSync('moth-create-'));
  tearDown(() => tmp.deleteSync(recursive: true));

  test('the generated app compiles', () {
    final dir = '${tmp.path}/glow';
    createProject(dir);
    final source = File('$dir/app.dart').readAsStringSync();
    final result = Compiler('$dir/app.dart', source).compile();
    expect(result.blob, isNotEmpty);
  });

  test('the scaffold is exactly the promised files', () {
    final dir = '${tmp.path}/glow';
    createProject(dir);
    final names = Directory(dir)
        .listSync()
        .map((e) => e.uri.pathSegments.lastWhere((s) => s.isNotEmpty))
        .toSet();
    expect(names, {'app.dart', 'README.md', '.gitignore'});
    // The README names the project, not a placeholder.
    expect(File('$dir/README.md').readAsStringSync(), contains('# glow'));
    expect(File('$dir/README.md').readAsStringSync(),
        isNot(contains('%NAME%')));
  });

  test('a non-empty target is refused, and left untouched', () {
    final dir = '${tmp.path}/taken';
    Directory(dir).createSync();
    File('$dir/precious.txt').writeAsStringSync('mine');
    expect(() => createProject(dir), throwsA(isA<CreateError>()));
    expect(File('$dir/precious.txt').readAsStringSync(), 'mine');
    expect(File('$dir/app.dart').existsSync(), isFalse);
  });

  test('an existing but empty directory is fine', () {
    final dir = '${tmp.path}/empty';
    Directory(dir).createSync();
    createProject(dir);
    expect(File('$dir/app.dart').existsSync(), isTrue);
  });
}
