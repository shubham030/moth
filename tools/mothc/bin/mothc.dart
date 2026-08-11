import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:analyzer/source/line_info.dart';
import 'package:mothc/src/compiler.dart';
import 'package:mothc/src/errors.dart';
import 'package:mothc/src/serial.dart';

const _usage = '''
mothc — compile Dart to moth bytecode

usage: mothc <input.dart> [-o output.mothb] [--push HOST:PORT | --push SERIAL]

  --push  send the compiled program to a running host, which stops what it
          is doing and starts this instead. The display stays up.
          HOST:PORT pushes over the network; a serial device path
          (/dev/cu.usbmodemXXXX) pushes over the USB cable — no WiFi needed.
''';

Future<void> main(List<String> args) async {
  if (args.isEmpty || args.contains('-h') || args.contains('--help')) {
    stdout.write(_usage);
    exit(args.isEmpty ? 64 : 0);
  }

  String? input;
  String? output;
  String? push;
  for (var i = 0; i < args.length; i++) {
    if (args[i] == '-o') {
      if (i + 1 >= args.length) {
        stderr.writeln('mothc: -o needs a file name');
        exit(64);
      }
      output = args[++i];
    } else if (args[i] == '--push') {
      if (i + 1 >= args.length) {
        stderr.writeln('mothc: --push needs HOST:PORT or a serial device');
        exit(64);
      }
      push = args[++i];
    } else if (input == null) {
      input = args[i];
    } else {
      stderr.writeln('mothc: unexpected argument "${args[i]}"');
      exit(64);
    }
  }

  if (input == null) {
    stderr.writeln('mothc: no input file');
    exit(64);
  }
  final file = File(input);
  if (!file.existsSync()) {
    stderr.writeln('mothc: no such file: $input');
    exit(66);
  }

  final source = file.readAsStringSync();
  final compiler = Compiler(input, source);
  try {
    final result = compiler.compile();
    final outPath =
        output ?? input.replaceAll(RegExp(r'\.dart$'), '') + '.mothb';
    File(outPath).writeAsBytesSync(result.blob);
    stdout.writeln('wrote $outPath (${result.blob.length} bytes)');
    if (push != null) {
      // Awaited, so a throw anywhere in the push surfaces as a message and
      // an exit code — unawaited, a missing stty or a yanked cable printed
      // a raw async stack trace, and main could not tell pushed from
      // still-in-flight.
      await _push(push, result.blob);
    }
  } on CompileError catch (e) {
    // The error may have come from an imported file, so report it against
    // whichever unit the compiler was processing.
    final unit = compiler.currentUnit;
    if (unit != null) {
      stderr.write(e.format(unit.path, unit.source, unit.lineInfo));
    } else {
      stderr.write(e.format(input, source, LineInfo.fromContent(source)));
    }
    exit(65);
  }
}

/// "MPSH", u32 length, u32 nonce, then the blob — vm/host/push_proto.h is
/// the format's home. The nonce comes back in the receiver's verdict reply,
/// which is what makes the reply unforgeable: no log line, no program's own
/// print(), and no stale reply from an earlier push can contain a random
/// number invented after all of them.
Uint8List _frame(Uint8List blob, int nonce) {
  final b = BytesBuilder()
    ..add(ascii.encode('MPSH'))
    ..add([
      blob.length & 0xFF,
      (blob.length >> 8) & 0xFF,
      (blob.length >> 16) & 0xFF,
      (blob.length >> 24) & 0xFF,
      nonce & 0xFF,
      (nonce >> 8) & 0xFF,
      (nonce >> 16) & 0xFF,
      (nonce >> 24) & 0xFF,
    ])
    ..add(blob);
  return b.toBytes();
}

Uint8List _reply(String cc, int nonce) => Uint8List.fromList([
      ...ascii.encode(cc),
      nonce & 0xFF,
      (nonce >> 8) & 0xFF,
      (nonce >> 16) & 0xFF,
      (nonce >> 24) & 0xFF,
    ]);

/// Scans [seen] for either verdict carrying [nonce]. Returns true for MPOK,
/// false for MPRJ, null for neither yet.
bool? _verdictIn(List<int> seen, int nonce) {
  bool contains(Uint8List needle) {
    outer:
    for (var i = 0; i + needle.length <= seen.length; i++) {
      for (var j = 0; j < needle.length; j++) {
        if (seen[i + j] != needle[j]) continue outer;
      }
      return true;
    }
    return false;
  }

  if (contains(_reply('MPOK', nonce))) return true;
  if (contains(_reply('MPRJ', nonce))) return false;
  return null;
}

/// Pushes over a serial device — the board's USB console doubles as a push
/// transport, so a cable is enough and no WiFi setup is needed.
///
/// The port handling lives in SerialPort (lib/src/serial.dart): nonblocking
/// open and raw mode via FFI, because Dart's File API can block forever on a
/// modem-class device and stty had the same flaw one subprocess removed.
///
/// The verdict is a framed binary reply carrying this push's nonce, so no
/// draining, no log-line grepping, and no ambiguity about which push a
/// reply answers — text acks lost that game three review rounds running.
/// No reply can mean the board was resetting when the port opened — some
/// adapters toggle the reset lines on open — so the frame is sent again
/// after a boot's worth of waiting before giving up; the nonce makes the
/// retry safe, since a late reply to attempt one still names this push.
Future<void> _pushSerial(String device, Uint8List blob) async {
  final SerialPort port;
  try {
    port = SerialPort.open(device);
  } on SerialException catch (e) {
    stderr.writeln('mothc: $e');
    exit(74);
  }

  final started = DateTime.now();
  final nonce = Random.secure().nextInt(0x100000000);
  final frame = _frame(blob, nonce);
  final seen = <int>[];
  for (var attempt = 1; attempt <= 3; attempt++) {
    port.writeAll(frame);
    final deadline = DateTime.now().add(const Duration(seconds: 4));
    while (DateTime.now().isBefore(deadline)) {
      final chunk = port.readAvailable();
      if (chunk.isEmpty) {
        sleep(const Duration(milliseconds: 20));
        continue;
      }
      seen.addAll(chunk);
      final verdict = _verdictIn(seen, nonce);
      if (verdict == true) {
        final ms = DateTime.now().difference(started).inMilliseconds;
        stdout.writeln('pushed over $device in ${ms}ms');
        exit(0);
      }
      if (verdict == false) {
        stderr.writeln('mothc: the board rejected the program — see its log');
        exit(70);
      }
    }
    if (attempt < 3) {
      stderr.writeln('mothc: no reply — the board may be booting; retrying');
      sleep(const Duration(seconds: 5));
    }
  }
  stderr.writeln('mothc: no reply from $device after 3 attempts');
  exit(70);
}

Future<void> _pushTcp(String host, int port, Uint8List blob) async {
  final started = DateTime.now();
  final nonce = Random.secure().nextInt(0x100000000);
  final socket =
      await Socket.connect(host, port, timeout: const Duration(seconds: 5));
  socket.add(_frame(blob, nonce));
  await socket.flush();

  // The framed verdict arrives AFTER the host verified the blob — so
  // "pushed" here means "verified and about to run", not merely
  // "delivered". Anything else listening on the port fails this check.
  final seen = <int>[];
  bool? verdict;
  try {
    await for (final chunk
        in socket.timeout(const Duration(seconds: 5), onTimeout: (sink) {
      sink.close();
    })) {
      seen.addAll(chunk);
      verdict = _verdictIn(seen, nonce);
      if (verdict != null) break;
    }
  } on SocketException {
    // A peer that closes without a verdict is handled below.
  }
  socket.destroy();
  if (verdict == null) {
    stderr.writeln('mothc: no verdict from $host:$port — is that a moth '
        'host? (the frame was sent, but nothing confirmed it)');
    exit(70);
  }
  if (verdict == false) {
    stderr.writeln('mothc: the host rejected the program — see its log');
    exit(70);
  }
  final ms = DateTime.now().difference(started).inMilliseconds;
  stdout.writeln('pushed to $host:$port in ${ms}ms');
  exit(0);
}

/// Sends a compiled program to a listening host: "MPSH", a little-endian
/// length, then the bytes. The host verifies before running it.
///
/// The transport is picked by what the target *is*, not by how it is
/// spelled: a serial device exists on the filesystem, and testing the
/// spelling routed `COM3:` (a common Windows form, colon included) to the
/// network branch and rejected `./ttyUSB0` outright.
Future<void> _push(String target, Uint8List blob) async {
  // A COM port is exactly COM + digits — a bare prefix test swallowed
  // hostnames like com.example.local:7621.
  if (Platform.isWindows &&
      RegExp(r'^COM\d+$', caseSensitive: false).hasMatch(target)) {
    stderr.writeln('mothc: serial push is not implemented on Windows yet — '
        'push over WiFi (HOST:PORT) instead');
    exit(74);
  }
  if (File(target).existsSync()) {
    try {
      await _pushSerial(target, blob);
    } on SerialException catch (e) {
      stderr.writeln('mothc: serial push failed — $e');
      exit(74);
    }
    return;
  }

  final colon = target.lastIndexOf(':');
  final port = colon < 0 ? null : int.tryParse(target.substring(colon + 1));
  if (port == null) {
    stderr.writeln('mothc: --push wants HOST:PORT or an existing serial '
        'device path (got "$target")');
    exit(64);
  }
  final host = target.substring(0, colon);
  try {
    await _pushTcp(host, port, blob);
  } on SocketException catch (e) {
    stderr.writeln('mothc: could not push to $host:$port — ${e.message}');
    exit(70);
  }
}
