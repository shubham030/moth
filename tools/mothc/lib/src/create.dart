/// `mothc create <dir>` — a new moth project, ready to run.
///
/// The scaffold is one Dart file, a README and a .gitignore, because that is
/// genuinely all a moth project is: the compiler resolves `package:moth/...`
/// from the packages shipped beside it, so there is no pubspec, no
/// `dart pub get`, no build configuration. The first program someone writes
/// should stay a single file; the scaffold's job is a working starting point
/// and the run commands, not machinery.
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
// Run it in a desktop window (from a moth checkout):
//
//   make ui F=%NAME%/app.dart
//
// Push it to a board over the USB cable — the display updates in under a
// second, no reflashing:
//
//   mothc app.dart --push /dev/cu.usbmodemXXXX
//
// Or over WiFi once the board is provisioned (see docs/getting-started.md):
//
//   mothc app.dart --push 192.168.x.x:7621 --token

import 'package:moth/widgets.dart';

class App extends Component {
  int taps = 0;

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

Everything lives in `app.dart`. There is no pubspec and nothing to install:
the moth compiler resolves `package:moth/...` by itself.

## Run it

In a desktop window (from a moth checkout):

    make ui F=path/to/%NAME%/app.dart

On a board over the USB cable:

    mothc app.dart --push /dev/cu.usbmodemXXXX

Over WiFi, once the board is provisioned with `tools/provision`:

    mothc app.dart --push <board-ip>:7621 --token

`--token` asks for the board's pairing phrase. Serial pushes never need it —
the cable is the pairing.
''';

const gitignoreTemplate = '''
*.mothb
''';

/// Creates the project directory and its files. Throws [CreateError] with a
/// human message when the target cannot be used — for every failure shape,
/// because this is the first command a beginner runs and a raw Dart stack
/// trace is not a first impression.
void createProject(String dir) {
  // Directory.existsSync() is false for a regular FILE at the path, which
  // would skip the guard and let createSync throw ENOTDIR — and "I typed a
  // filename instead of a directory name" is exactly the mistake this
  // command's audience makes.
  final kind = FileSystemEntity.typeSync(dir);
  if (kind == FileSystemEntityType.file || kind == FileSystemEntityType.link) {
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
    ]) {
      final f = File(p.join(dir, fileName));
      written.add(f);
      f.writeAsStringSync(content);
    }
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

class CreateError implements Exception {
  final String message;
  CreateError(this.message);
  @override
  String toString() => message;
}
