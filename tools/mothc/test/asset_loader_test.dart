// The loader's asset section parses input that arrives over the network, so
// every rejection it promises needs a blob that proves it. These are built
// by compiling a real (asset-free) program — whose blob ends with an empty
// asset table — and replacing that tail with hand-crafted hostile sections.
// The surrounding tables stay valid; the damage is confined to the thing
// under test, same as verifier_test.dart.
//
// The size clamp matters most: w,h <= 2048 is what keeps npix*4 inside
// 32-bit size_t on the ESP32, so someone raising it for a bigger panel must
// meet a failing test, not a green suite.
import 'dart:io';
import 'dart:typed_data';

import 'package:test/test.dart';

void main() {
  final repoRoot = Directory.current.path.endsWith('tools/mothc')
      ? Directory.current.parent.parent.path
      : Directory.current.path;
  final mothrun = File('$repoRoot/build/vm/mothrun');

  late Uint8List base; // a valid blob whose last two bytes are nassets == 0
  late int stringConst; // index of a known string constant in its pool

  setUpAll(() {
    if (!mothrun.existsSync()) {
      fail('build/vm/mothrun not found — run: cmake -B build . && '
          'cmake --build build');
    }
    final dir = Directory.systemTemp.createTempSync('moth_assets');
    final src = File('${dir.path}/tiny.dart')
      ..writeAsStringSync('void main() { var x = 7; }\n');
    final out = '${dir.path}/tiny.mothb';
    final r = Process.runSync('dart',
        ['run', '$repoRoot/tools/mothc/bin/mothc.dart', src.path, '-o', out]);
    if (r.exitCode != 0) fail('mothc failed: ${r.stderr}');
    base = File(out).readAsBytesSync();
    expect(base[base.length - 2] | base[base.length - 1], 0,
        reason: 'expected the blob to end with an empty asset table');

    // Find a real string constant by walking the pool, so the happy-path
    // pieces of a crafted section can name a valid key.
    var o = 8; // magic + version + flags
    final nconsts = base[o] | (base[o + 1] << 8);
    o += 2;
    stringConst = -1;
    for (var i = 0; i < nconsts; i++) {
      final tag = base[o++];
      if (tag == 0 || tag == 1) {
        o += 8;
      } else if (tag == 2) {
        final len = base[o] | (base[o + 1] << 8);
        o += 2 + len;
        stringConst = i;
        break;
      } else if (tag == 3) {
        o += 1;
      } // tag 4 (null) has no payload
    }
    expect(stringConst, isNot(-1),
        reason: 'the pool should contain at least one string ("main")');
  });

  /// Replaces the empty asset table with [tail] and runs mothrun on it.
  String runWithAssetTail(List<int> tail) {
    final blob = Uint8List(base.length - 2 + tail.length)
      ..setRange(0, base.length - 2, base)
      ..setRange(base.length - 2, base.length - 2 + tail.length, tail);
    final f = File(
        '${Directory.systemTemp.createTempSync('moth_assets').path}/h.mothb')
      ..writeAsBytesSync(blob);
    final r = Process.runSync(mothrun.path, [f.path]);
    expect(r.exitCode, isNot(0), reason: 'a hostile blob must be refused');
    return '${r.stdout}${r.stderr}';
  }

  List<int> u16(int v) => [v & 0xFF, (v >> 8) & 0xFF];

  /// Pad bytes so pixel data lands 4-byte aligned from the blob start,
  /// mirroring the writer's rule for a record beginning right after the
  /// count at (base.length - 2).
  List<int> padFor(int recordBytes) {
    final off = base.length - 2 + 2 + recordBytes; // + count + record
    final pad = (4 - (off & 3)) & 3;
    return List.filled(pad, 0);
  }

  test('a missing asset table is refused', () {
    // Strip the count entirely: v6 requires it.
    final blob = Uint8List.sublistView(base, 0, base.length - 2);
    final f = File(
        '${Directory.systemTemp.createTempSync('moth_assets').path}/t.mothb')
      ..writeAsBytesSync(blob);
    final r = Process.runSync(mothrun.path, [f.path]);
    expect(r.exitCode, isNot(0));
    expect('${r.stdout}${r.stderr}', contains('truncated asset table'));
  });

  test('a truncated asset record is refused', () {
    final out = runWithAssetTail([...u16(1), ...u16(stringConst), 0x02]);
    expect(out, contains('truncated asset 0'));
  });

  test('an absurd size is refused — the overflow guard', () {
    // 0 wide, and (separately) far past the 2048 clamp.
    var out = runWithAssetTail(
        [...u16(1), ...u16(stringConst), ...u16(0), ...u16(1)]);
    expect(out, contains('not a sane size'));
    out = runWithAssetTail(
        [...u16(1), ...u16(stringConst), ...u16(60000), ...u16(60000)]);
    expect(out, contains('not a sane size'));
  });

  test('a key that is not a string constant is refused', () {
    final out = runWithAssetTail([
      ...u16(1), ...u16(0xFFFF), ...u16(1), ...u16(1), //
      ...padFor(6), 0, 0, 0, 0,
    ]);
    expect(out, contains('key is not a string constant'));
  });

  test('pixel data shorter than width*height is refused', () {
    // Claims 4x4 (64 bytes) but supplies 8.
    final out = runWithAssetTail([
      ...u16(1), ...u16(stringConst), ...u16(4), ...u16(4), //
      ...padFor(6), 1, 2, 3, 4, 5, 6, 7, 8,
    ]);
    expect(out, contains('pixel data truncated'));
  });

  test('and a well-formed section still loads', () {
    // The control: 1x1 with exactly 4 pixel bytes runs fine.
    final blob = Uint8List.fromList([
      ...Uint8List.sublistView(base, 0, base.length - 2),
      ...u16(1), ...u16(stringConst), ...u16(1), ...u16(1), //
      ...padFor(6), 0x11, 0x22, 0x33, 0xFF,
    ]);
    final f = File(
        '${Directory.systemTemp.createTempSync('moth_assets').path}/ok.mothb')
      ..writeAsBytesSync(blob);
    final r = Process.runSync(mothrun.path, [f.path]);
    expect(r.exitCode, 0, reason: '${r.stdout}${r.stderr}');
  });
}
