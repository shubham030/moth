/// `mothc create <dir>` — a new moth project, ready to run.
///
/// The code is one Dart file, because that is genuinely all a moth program
/// is: mothc resolves `package:moth/...` from the packages shipped beside
/// it and never reads a pubspec. The rest of the scaffold exists for the
/// EDITOR — a pubspec and analysis options so the Dart analyser resolves
/// the same import mothc does, which is the difference between an app that
/// opens with autocomplete and one that opens as a wall of red squiggles.
///
/// The template is compiled in create_test.dart on every `make test` run —
/// a scaffold that greets a beginner with a compile error is worse than no
/// scaffold, and the widget API it uses is still settling.
library;

import 'dart:io';

import 'package:path/path.dart' as p;

/// The starter program. Deliberately the classic counter: a Flutter person
/// recognizes the shape in one glance, and the tap -> setState -> rebuild
/// loop is the one concept everything else builds on.
const appTemplate = '''
// A moth app: Flutter's programming model on a microcontroller.
//
// Run it:
//
//   moth run
//
// A connected board is picked up automatically (the simulator if there is
// none); press r after an edit and the display updates in well under a
// second — no reflashing. See docs/getting-started.md for WiFi pushes.

import 'package:moth/widgets.dart';

class App extends Component {
  int taps = 0;

  @override
  Widget build() {
    return GestureDetector(
      onTap: () {
        setState(() {
          taps += 1;
        });
      },
      child: Container(
        color: 0xFF16161E,
        flex: 1,
        padding: uiSafeArea(0),
        child: Column(
          mainAxisAlignment: mainAxisCenter,
          crossAxisAlignment: crossAxisCenter,
          spacing: 16,
          children: [
            Text('\$taps', style: TextStyle(fontSize: 72, color: 0xFFF2EFE7)),
            Text(taps == 0 ? 'tap the screen' : 'keep going',
                style: TextStyle(fontSize: 16, color: 0xFF9AA2B8)),
          ],
        ),
      ),
    );
  }
}

void main() {
  runApp(App());
  var last = millis();
  while (true) {
    var now = millis();
    pumpFrame(now - last);
    last = now;
    delay(16);
  }
}
''';

const readmeTemplate = '''
# %NAME%

A [moth](https://github.com/shubham030/moth) app — Dart and Flutter's
programming model, running on a \$6 microcontroller.

Everything you write lives in `app.dart`. Keep the `pubspec.yaml` beside
it: it is how `package:moth` gets resolved — by your editor for
autocomplete and error-checking, and by the compiler itself whenever you
are outside a moth checkout (`pub get` writes the package config mothc
reads).

## Run it

    moth run

A connected board is picked up automatically; with no board it opens the
desktop simulator. While it runs, press `r` to push your latest edit to the
display — a hot restart, well under a second, no reflashing.

Over WiFi, once the board is provisioned with `tools/provision`:

    mothc app.dart --push <board-ip>:7621 --token

`--token` asks for the board's pairing phrase. Serial pushes never need it —
the cable is the pairing.

## Check it as you type

    moth check app.dart

moth runs a subset of Dart, and the compiler is what knows the difference.
In VS Code this is the default build task (Cmd/Ctrl-Shift-B).
''';

const gitignoreTemplate = '''
*.mothb
.dart_tool/
''';

/// A pubspec exists for the EDITOR, not for mothc — the compiler resolves
/// `package:moth` by itself and never reads this file. Without it, though,
/// the Dart analyser cannot resolve the import, so a beginner's first
/// project opens as a wall of red squiggles with autocomplete dead. %DEP%
/// is filled in with a path dependency when the moth checkout is next
/// door, and a hosted one otherwise.
const pubspecTemplate = '''
name: %NAME%
description: A moth app.
publish_to: none
version: 0.1.0

environment:
  sdk: ^3.5.0

dependencies:
%DEP%

dev_dependencies:
  lints: ^5.0.0
''';

/// The stock Dart lints. moth's own subset rules are enforced by the
/// compiler (and reported with hints); these catch the ordinary mistakes
/// every Dart program can make.
const analysisOptionsTemplate = '''
include: package:lints/recommended.yaml

analyzer:
  errors:
    # moth programs run one program per board; an unused import is usually
    # a leftover rather than a mistake worth failing over.
    unused_import: warning
''';

/// Ctrl/Cmd-Shift-B compiles, so the subset's compile-time rejections show
/// up without leaving the editor.
// Raw: the JSON carries both VS Code's ${workspaceFolder} and a regex whose
// backslashes and $ must survive verbatim.
const vscodeTasksTemplate = r'''
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "moth: check",
      "type": "shell",
      "command": "mothc check app.dart",
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": {
        "owner": "moth",
        "fileLocation": ["relative", "${workspaceFolder}"],
        "pattern": {
          "regexp": "^(.*):(\\d+):(\\d+): (.*)$",
          "file": 1,
          "line": 2,
          "column": 3,
          "message": 4
        }
      }
    }
  ]
}
''';

/// Creates the project directory and its files. Throws [CreateError] with a
/// human message when the target cannot be used — for every failure shape,
/// because this is the first command a beginner runs and a raw Dart stack
/// trace is not a first impression.
///
/// Returns true when the pubspec's moth dependency points at a real
/// checkout — false means the placeholder was written and the CLI should
/// say so instead of promising resolution.
bool createProject(String dir) {
  // Directory.existsSync() is false for a regular FILE at the path, which
  // would skip the guard and let createSync throw ENOTDIR — and "I typed a
  // filename instead of a directory name" is exactly the mistake this
  // command's audience makes.
  // typeSync follows links, so a symlink reports its target's type: a link
  // to a file hits this refusal, a link to a directory is treated as one,
  // and a dangling link falls to the write path's CreateError.
  final kind = FileSystemEntity.typeSync(dir);
  if (kind == FileSystemEntityType.file) {
    throw CreateError("'$dir' already exists and is a file — create wants a "
        'directory name');
  }
  final target = Directory(dir);
  final existed = kind != FileSystemEntityType.notFound;
  if (existed && target.listSync().isNotEmpty) {
    throw CreateError("'$dir' exists and is not empty — pick a new name, or "
        'an empty directory');
  }
  final name = p.basename(p.normalize(dir));
  // Everything after this point cleans up behind itself: a half-written
  // scaffold (full disk, revoked permissions) would make every retry hit
  // the not-empty refusal above with no hint of why.
  final written = <File>[];
  try {
    target.createSync(recursive: true);
    for (final (fileName, content) in [
      ('app.dart', appTemplate.replaceAll('%NAME%', name)),
      ('README.md', readmeTemplate.replaceAll('%NAME%', name)),
      ('.gitignore', gitignoreTemplate),
      (
        'pubspec.yaml',
        pubspecTemplate
            .replaceAll('%NAME%', _pubName(name))
            .replaceAll('%DEP%', _mothDependency(dir))
      ),
      ('analysis_options.yaml', analysisOptionsTemplate),
      (p.join('.vscode', 'tasks.json'), vscodeTasksTemplate),
    ]) {
      final f = File(p.join(dir, fileName));
      f.parent.createSync(recursive: true);
      written.add(f);
      f.writeAsStringSync(content);
    }
    return _findMothPackage() != null;
  } on FileSystemException catch (e) {
    for (final f in written) {
      try {
        f.deleteSync();
      } on FileSystemException {
        // Cleanup is best-effort; the original error is the one to report.
      }
    }
    if (!existed) {
      try {
        target.deleteSync();
      } on FileSystemException {
        // Same: report why the scaffold failed, not why cleanup did.
      }
    }
    throw CreateError(
        "cannot write into '$dir' — ${e.osError?.message ?? e.message}");
  }
}

/// A directory name is not always a legal pub package name (`my-app`,
/// `2048`). The pubspec needs one, and nothing else depends on it, so it is
/// sanitized rather than rejected.
String _pubName(String dirName) {
  var s = dirName.toLowerCase().replaceAll(RegExp(r'[^a-z0-9_]'), '_');
  if (s.isEmpty || !RegExp(r'^[a-z]').hasMatch(s)) s = 'moth_$s';
  return s;
}

/// The dependency line for `package:moth`.
///
/// A path dependency when the moth checkout is next door — which is every
/// case today, since that is the only way to have mothc at all — and a
/// hosted one otherwise, which is what a `dart pub global activate` install
/// will want once moth is published.
String _mothDependency(String projectDir) {
  final found = _findMothPackage();
  // A pub-global mothc has no checkout beside it; the hosted package is
  // the published one and pub verifies its integrity. Inside a checkout
  // the path dependency wins, so hacking on package:moth is reflected
  // immediately.
  if (found == null) return '  moth: ^0.1.0';
  // Relative when the project sits near the checkout, so moving the pair
  // together keeps working. A project created somewhere unrelated would
  // otherwise get a ../../../../.. chain that is unreadable and no more
  // portable than the absolute path.
  final rel = p.relative(found, from: p.absolute(projectDir));
  final up = RegExp(r'\.\.[/\\]').allMatches(rel).length;
  final dep = up > 3 ? found : rel;
  return '  moth:\n    path: $dep';
}

/// The directory of `packages/moth` in a moth checkout, or null.
String? _findMothPackage() {
  // Same anchors the compiler resolves package: imports against, so the
  // editor and mothc cannot end up pointed at different copies.
  final fromScript =
      p.dirname(p.dirname(p.absolute(Platform.script.toFilePath())));
  final cwd = Directory.current.path;
  for (final guess in [
    p.join(fromScript, '..', '..', 'packages', 'moth'),
    p.join(fromScript, '..', 'packages', 'moth'),
    p.join(cwd, 'packages', 'moth'),
    p.join(cwd, '..', 'packages', 'moth'),
    p.join(cwd, '..', '..', 'packages', 'moth'),
  ]) {
    final dir = p.normalize(guess);
    if (File(p.join(dir, 'lib', 'widgets.dart')).existsSync()) return dir;
  }
  return null;
}

/// Fetches dependencies so the editor can resolve `package:moth`
/// immediately. Best-effort: the scaffold is valid without it, and the
/// caller reports what happened rather than failing the create.
bool resolveProject(String dir) {
  try {
    final r = Process.runSync('dart', ['pub', 'get'], workingDirectory: dir);
    return r.exitCode == 0;
  } on ProcessException {
    return false; // no Dart SDK on PATH; mothc still compiles the project
  }
}

class CreateError implements Exception {
  final String message;
  CreateError(this.message);
  @override
  String toString() => message;
}
