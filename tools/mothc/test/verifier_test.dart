// The verifier's job is to refuse a blob that would misbehave at runtime, so
// testing it needs blobs the compiler would never emit. These are built by
// compiling a real program and overwriting main's code with a chosen opcode
// sequence — the surrounding tables stay valid, which is the point: the
// damage is confined to the thing under test.
//
//   dart test test/verifier_test.dart

import 'dart:io';

import 'package:test/test.dart';

// Opcodes, from vm/src/internal.h.
const opInt8 = 0x02;
const opTrue = 0x03;
const opStore = 0x09;
const opAdd = 0x10;
const opRet = 0x42;
const opRetNull = 0x43;

void main() {
  final repoRoot = Directory.current.path.endsWith('tools/mothc')
      ? '${Directory.current.parent.parent.path}'
      : Directory.current.path;
  final mothrun = File('$repoRoot/build/vm/mothrun');

  late Uint8ListHolder base;
  late int codeAt;

  setUpAll(() {
    if (!mothrun.existsSync()) {
      fail('build/vm/mothrun not found — run: cmake -B build . && '
          'cmake --build build');
    }
    final dir = Directory.systemTemp.createTempSync('moth_verify');
    final src = File('${dir.path}/tiny.dart')
      ..writeAsStringSync('void main() { var x = 7; }\n');
    final out = '${dir.path}/tiny.mothb';
    final r = Process.runSync('dart', [
      'run',
      '$repoRoot/tools/mothc/bin/mothc.dart',
      src.path,
      '-o',
      out,
    ]);
    if (r.exitCode != 0) fail('mothc failed: ${r.stderr}');

    base = Uint8ListHolder(File(out).readAsBytesSync());
    // main compiles to: INT8 7; STORE 0; RET_NULL — five bytes to overwrite.
    codeAt = base.indexOf([opInt8, 7, opStore, 0, opRetNull]);
    expect(codeAt, isNot(-1), reason: "could not find main's code in the blob");
  });

  /// Runs [code] as main's body and returns whatever mothrun printed.
  String runWith(List<int> code) {
    final patched = base.patched(codeAt, code);
    final f = File(
        '${Directory.systemTemp.createTempSync('moth_verify').path}/p.mothb')
      ..writeAsBytesSync(patched);
    final r = Process.runSync(mothrun.path, [f.path]);
    return '${r.stdout}${r.stderr}'.trim();
  }

  test('an arithmetic op with one operand is refused before it runs', () {
    // OP_ADD reads two values with only one pushed. Terminating with RET_NULL
    // matters: OP_RET carries its own depth check and would mask this by
    // rejecting the program one instruction later, for the wrong reason.
    final out = runWith([opTrue, opAdd, opRetNull, 0, 0]);
    expect(out, contains('stack underflow'));
    // Offset 1 is the OP_ADD itself, not the instruction after it.
    expect(out, contains('offset 1'));
  });

  test('the same sequence ending in RET is caught at the ADD, not the RET', () {
    // Guards against the fix regressing to "some later opcode happens to
    // notice", which is what made the original report look closed.
    final out = runWith([opTrue, opAdd, opRet, 0, 0]);
    expect(out, contains('offset 1'));
  });

  test('a well-formed program still loads', () {
    final out = runWith([opInt8, 7, opStore, 0, opRetNull]);
    expect(out, isNot(contains('stack underflow')));
  });
}

/// Small helper so the tests read as "the blob, with these bytes swapped in".
class Uint8ListHolder {
  Uint8ListHolder(this.bytes);
  final List<int> bytes;

  int indexOf(List<int> needle) {
    for (var i = 0; i + needle.length <= bytes.length; i++) {
      var hit = true;
      for (var j = 0; j < needle.length; j++) {
        if (bytes[i + j] != needle[j]) {
          hit = false;
          break;
        }
      }
      if (hit) return i;
    }
    return -1;
  }

  List<int> patched(int at, List<int> replacement) {
    final copy = List<int>.from(bytes);
    for (var i = 0; i < replacement.length; i++) {
      copy[at + i] = replacement[i];
    }
    return copy;
  }
}
