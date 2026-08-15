// The non-interactive halves of `mothc run`: device selection (the choice
// the tool makes silently must be the obvious one) and the display filter
// (verdict frames must vanish from the console without eating log bytes).
import 'dart:typed_data';

import 'package:mothc/src/devices.dart';
import 'package:mothc/src/run.dart';
import 'package:test/test.dart';

Device board(String id) => Device(id, DeviceKind.board, 'serial board');
Device sim() => Device('sim', DeviceKind.simulator, 'desktop simulator');

void main() {
  group('selectDevice', () {
    test('one board auto-selects even when the simulator exists', () {
      final d = selectDevice([board('/dev/cu.usbmodem2101'), sim()], null);
      expect(d?.id, '/dev/cu.usbmodem2101');
    });

    test('no board falls back to the simulator', () {
      expect(selectDevice([sim()], null)?.kind, DeviceKind.simulator);
    });

    test('two boards refuse to guess', () {
      final d =
          selectDevice([board('/dev/cu.usbmodemA'), board('/dev/cu.usbmodemB')], null);
      expect(d, isNull);
    });

    test('-d matches a unique substring, so the /dev/cu. spelling is optional',
        () {
      final devices = [board('/dev/cu.usbmodem2101'), sim()];
      expect(selectDevice(devices, 'usbmodem2101')?.id, '/dev/cu.usbmodem2101');
      expect(selectDevice(devices, 'sim')?.kind, DeviceKind.simulator);
      expect(selectDevice(devices, 'nope'), isNull);
    });

    test('-d that matches several is refused rather than guessed', () {
      final devices = [board('/dev/cu.usbmodemA'), board('/dev/cu.usbmodemB')];
      expect(selectDevice(devices, 'usbmodem'), isNull);
    });
  });

  group('VerdictDisplayFilter', () {
    Uint8List verdict(String cc, int nonce) => Uint8List.fromList([
          ...cc.codeUnits,
          nonce & 0xFF,
          (nonce >> 8) & 0xFF,
          (nonce >> 16) & 0xFF,
          (nonce >> 24) & 0xFF,
        ]);

    test('a verdict frame vanishes; surrounding log bytes survive', () {
      final f = VerdictDisplayFilter();
      final input = Uint8List.fromList([
        ...'boot ok\n'.codeUnits,
        ...verdict('MPOK', 0x12345678),
        ...'running\n'.codeUnits,
      ]);
      expect(String.fromCharCodes(f.filter(input)), 'boot ok\nrunning\n');
    });

    test('the triple-sent copies all vanish', () {
      final f = VerdictDisplayFilter();
      final v = verdict('MPRJ', 7);
      final input = Uint8List.fromList([...v, ...v, ...v, ...'log'.codeUnits]);
      expect(String.fromCharCodes(f.filter(input)), 'log');
    });

    test('a frame split across two chunks still vanishes', () {
      final f = VerdictDisplayFilter();
      final v = verdict('MPOK', 0xAABBCCDD);
      final a = f.filter(Uint8List.fromList(v.sublist(0, 3)));
      final b = f.filter(Uint8List.fromList([...v.sublist(3), ...'x'.codeUnits]));
      expect(String.fromCharCodes(a) + String.fromCharCodes(b), 'x');
    });

    test('an innocent MP in text is not eaten', () {
      final f = VerdictDisplayFilter();
      final input = Uint8List.fromList('temp MPa reading: 4 MPH!\n'.codeUnits);
      expect(String.fromCharCodes(f.filter(input)), 'temp MPa reading: 4 MPH!\n');
    });

    test('MP at the end of a chunk is held, then released when innocent', () {
      final f = VerdictDisplayFilter();
      final a = f.filter(Uint8List.fromList('speed: MP'.codeUnits));
      final b = f.filter(Uint8List.fromList('H 88\n'.codeUnits));
      expect(String.fromCharCodes(a) + String.fromCharCodes(b), 'speed: MPH 88\n');
    });
  });
}
