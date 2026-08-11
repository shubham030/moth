// The push wire format and the verdict scanner — the parts of hot push that
// broke four review rounds running. Every failure mode a round found is a
// case here, so it cannot come back quietly.

import 'dart:convert';
import 'dart:typed_data';

import 'package:mothc/src/push_wire.dart';
import 'package:test/test.dart';

void main() {
  final blob = Uint8List.fromList(List.generate(300, (i) => i & 0xFF));

  test('frame layout matches push_proto.h: magic, length, nonce, blob', () {
    final f = pushFrame(blob, 0xAABBCCDD);
    expect(ascii.decode(f.sublist(0, 4)), 'MPSH');
    expect(f[4] | (f[5] << 8) | (f[6] << 16) | (f[7] << 24), blob.length);
    expect(f[8], 0xDD);
    expect(f[9], 0xCC);
    expect(f[10], 0xBB);
    expect(f[11], 0xAA);
    expect(f.sublist(12), blob);
  });

  test('a clean verdict is found', () {
    final s = VerdictScanner(42);
    expect(s.feed(verdictReply('MPOK', 42)), true);
    expect(VerdictScanner(42).feed(verdictReply('MPRJ', 42)), false);
  });

  test('a verdict split across chunks is still found', () {
    final s = VerdictScanner(7);
    final r = verdictReply('MPOK', 7);
    expect(s.feed(r.sublist(0, 3)), null);
    expect(s.feed(r.sublist(3)), true);
  });

  test('log noise around the verdict does not hide it', () {
    final s = VerdictScanner(9);
    expect(s.feed(ascii.encode('I (123) moth: running Dart UI\n')), null);
    expect(s.feed([...ascii.encode('garbage'), ...verdictReply('MPOK', 9)]),
        true);
  });

  test('the wrong nonce never matches — stale and forged replies are inert',
      () {
    final s = VerdictScanner(1);
    expect(s.feed(verdictReply('MPOK', 2)), null);
    expect(s.feed(verdictReply('MPRJ', 2)), null);
    // A program printing the ack text was round three's false ack.
    expect(s.feed(ascii.encode('moth: push: 2209 bytes received')), null);
  });

  test('scanning stays O(chunk): the window never grows past the carry', () {
    final s = VerdictScanner(3);
    for (var i = 0; i < 10000; i++) {
      expect(s.feed(ascii.encode('log line noise, forever and ever\n')), null);
    }
    // Still finds a verdict arriving after megabytes of noise.
    expect(s.feed(verdictReply('MPOK', 3)), true);
  });

  test('a verdict interleaved mid-reply by other bytes is NOT matched — '
      'which is why the board sends it three times', () {
    final s = VerdictScanner(5);
    final r = verdictReply('MPOK', 5);
    final corrupted = [...r.sublist(0, 4), 0x58, ...r.sublist(4)];
    expect(s.feed(corrupted), null);
    // The second, clean copy lands.
    expect(s.feed(r), true);
  });
}
