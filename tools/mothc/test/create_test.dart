// The scaffold's one hard promise: what `mothc create` writes COMPILES.
// A starter that greets a beginner with a compile error is worse than no
// starter, and the widget API the template leans on is still settling —
// this is the tripwire that keeps the two in step.
import 'dart:convert';
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
    expect(names, {
      'app.dart',
      'README.md',
      '.gitignore',
      'pubspec.yaml',
      'analysis_options.yaml',
      '.vscode',
    });
    expect(File('$dir/.vscode/tasks.json').existsSync(), isTrue);
    // The README names the project, not a placeholder.
    expect(File('$dir/README.md').readAsStringSync(), contains('# glow'));
    expect(
        File('$dir/README.md').readAsStringSync(), isNot(contains('%NAME%')));
  });

  test('the pubspec points at a resolvable package:moth', () {
    final dir = '${tmp.path}/glow';
    createProject(dir);
    final spec = File('$dir/pubspec.yaml').readAsStringSync();
    expect(spec, contains('name: glow'));
    expect(spec, contains('moth:'));
    // A path dependency must actually exist, or the editor resolves nothing
    // — the entire reason the pubspec is generated.
    final m = RegExp(r'path: (.+)').firstMatch(spec);
    if (m != null) {
      final dep = m.group(1)!.trim();
      final resolved = dep.startsWith('/') ? dep : '$dir/$dep';
      expect(File('$resolved/lib/widgets.dart').existsSync(), isTrue,
          reason:
              'the generated path dependency should point at packages/moth');
    }
  });

  test('the editor task file is valid JSON', () {
    final dir = '${tmp.path}/glow';
    createProject(dir);
    final task = File('$dir/.vscode/tasks.json').readAsStringSync();
    final parsed = jsonDecode(task) as Map<String, dynamic>;
    expect((parsed['tasks'] as List).first['command'], contains('mothc check'));
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

  test('a target that is a FILE gets a human refusal, and survives', () {
    // Directory.existsSync() is false for a file, which used to skip the
    // guard entirely and surface a raw ENOTDIR stack trace.
    final path = '${tmp.path}/notes.txt';
    File(path).writeAsStringSync('my notes');
    expect(
        () => createProject(path),
        throwsA(isA<CreateError>()
            .having((e) => e.message, 'message', contains('is a file'))));
    expect(File(path).readAsStringSync(), 'my notes');
  });

  test('a failed scaffold leaves a retryable target', () {
    // Sabotage: the target exists and is empty, but not writable — the
    // first file write fails. The refusal must be a CreateError, and the
    // directory must stay empty so the retry (permissions restored) works
    // instead of hitting the not-empty refusal.
    final dir = '${tmp.path}/locked';
    Directory(dir).createSync();
    Process.runSync('chmod', ['555', dir]);
    addTearDown(() => Process.runSync('chmod', ['755', dir]));
    expect(() => createProject(dir), throwsA(isA<CreateError>()));
    Process.runSync('chmod', ['755', dir]);
    createProject(dir); // the retry succeeds — nothing half-written survived
    expect(File('$dir/app.dart').existsSync(), isTrue);
  });
}
