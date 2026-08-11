// Opens the pty slave through SerialPort and round-trips bytes written by
// the python side on the master — exercising open(O_NONBLOCK), tcgetattr,
// the cflag-normalized tcsetattr, readAvailable and writeAll for real.
import 'dart:io';
import 'dart:typed_data';
import 'package:mothc/src/serial.dart';

void main(List<String> args) {
  final port = SerialPort.open(args[0]);
  // binary round-trip INCLUDING 0x0A and 0x0D — the bytes cooked mode mangles
  final probe = Uint8List.fromList([0x4D, 0x0A, 0x0D, 0x00, 0xFF, 0x7F]);
  port.writeAll(probe);
  final got = <int>[];
  final deadline = DateTime.now().add(const Duration(seconds: 3));
  while (got.length < probe.length && DateTime.now().isBefore(deadline)) {
    got.addAll(port.readAvailable());
    sleep(const Duration(milliseconds: 10));
  }
  port.close();
  if (got.length == probe.length &&
      List.generate(probe.length, (i) => got[i] == probe[i]).every((x) => x)) {
    print('SERIAL PTY TEST: PASS (raw round-trip exact)');
    exit(0);
  }
  print('SERIAL PTY TEST: FAIL — sent $probe got $got');
  exit(1);
}
