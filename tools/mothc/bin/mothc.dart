import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:analyzer/source/line_info.dart';
import 'package:mothc/src/compiler.dart';
import 'package:mothc/src/errors.dart';

const _usage = '''
mothc — compile Dart to moth bytecode

usage: mothc <input.dart> [-o output.mothb] [--push HOST:PORT | --push SERIAL]

  --push  send the compiled program to a running host, which stops what it
          is doing and starts this instead. The display stays up.
          HOST:PORT pushes over the network; a serial device path
          (/dev/cu.usbmodemXXXX) pushes over the USB cable — no WiFi needed.
''';

void main(List<String> args) {
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
        stderr.writeln('mothc: --push needs HOST:PORT');
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
      _push(push, result.blob);
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

Uint8List _frame(Uint8List blob) {
  final b = BytesBuilder()
    ..add(ascii.encode('MPSH'))
    ..add([
      blob.length & 0xFF,
      (blob.length >> 8) & 0xFF,
      (blob.length >> 16) & 0xFF,
      (blob.length >> 24) & 0xFF,
    ])
    ..add(blob);
  return b.toBytes();
}

/// Pushes over a serial device — the board's USB console doubles as a push
/// transport, so a cable is enough and no WiFi setup is needed.
///
/// Serial has no connection semantics, so the ack is the board's own log
/// line ("push: N bytes received") read back off the same port. No ack can
/// mean the board was resetting when the port opened — some adapters toggle
/// the reset lines on open — so the frame is sent again after a boot's worth
/// of waiting before giving up.
Future<void> _pushSerial(String device, Uint8List blob) async {
  // Raw mode, or the tty layer cooks the blob's bytes (\n becomes \r\n and
  // the program is corrupt). min 0 time 10 makes reads poll at 1s instead of
  // blocking forever on a silent board.
  final sttyFlag = Platform.isMacOS ? '-f' : '-F';
  final stty = await Process.run(
      'stty', [sttyFlag, device, 'raw', '-echo', 'min', '0', 'time', '10']);
  if (stty.exitCode != 0) {
    stderr.writeln('mothc: stty failed on $device — ${stty.stderr}');
    exit(74);
  }

  // Two handles: a tty cannot seek, and every read/write FileMode that
  // Dart offers on one handle wants to (append seeks to the end at open).
  // writeOnly and read do not seek, so the frame goes out one and the
  // board's ack comes back the other.
  final RandomAccessFile tx;
  final RandomAccessFile rx;
  try {
    tx = await File(device).open(mode: FileMode.writeOnly);
    rx = await File(device).open(mode: FileMode.read);
  } on FileSystemException catch (e) {
    stderr.writeln('mothc: cannot open $device — ${e.osError?.message}. '
        'Is a serial monitor holding it?');
    exit(74);
  }

  final started = DateTime.now();
  final frame = _frame(blob);
  for (var attempt = 1; attempt <= 3; attempt++) {
    await tx.writeFrom(frame);
    final deadline = DateTime.now().add(const Duration(seconds: 4));
    var seen = '';
    while (DateTime.now().isBefore(deadline)) {
      final chunk = await rx.read(256);
      if (chunk.isEmpty) continue; // 1s poll timeout from stty time 10
      seen += String.fromCharCodes(chunk);
      if (seen.contains('push: ')) {
        final ms = DateTime.now().difference(started).inMilliseconds;
        stdout.writeln('pushed over $device in ${ms}ms');
        exit(0);
      }
      if (seen.contains('push rejected')) {
        stderr.writeln('mothc: the board rejected the program — see its log');
        exit(70);
      }
    }
    if (attempt < 3) {
      stderr.writeln('mothc: no reply — the board may be booting; retrying');
      await Future<void>.delayed(const Duration(seconds: 5));
    }
  }
  stderr.writeln('mothc: no reply from $device after 3 attempts');
  exit(70);
}

/// Sends a compiled program to a listening host: "MPSH", a little-endian
/// length, then the bytes. The host verifies before running it.
void _push(String target, Uint8List blob) {
  final colon = target.lastIndexOf(':');
  if (colon < 0) {
    if (target.startsWith('/') || target.startsWith('COM')) {
      _pushSerial(target, blob);
      return;
    }
    stderr.writeln('mothc: --push wants HOST:PORT or a serial device path');
    exit(64);
  }
  final host = target.substring(0, colon);
  final port = int.tryParse(target.substring(colon + 1));
  if (port == null) {
    stderr.writeln('mothc: --push wants HOST:PORT');
    exit(64);
  }

  final started = DateTime.now();
  Socket.connect(host, port, timeout: const Duration(seconds: 5))
      .then((socket) async {
    socket.add(_frame(blob));
    await socket.flush();
    await socket.close();
    final ms = DateTime.now().difference(started).inMilliseconds;
    stdout.writeln('pushed to $host:$port in ${ms}ms');
    // Without this the process hangs: the socket's receive side is still
    // open and keeps the event loop alive even after close().
    exit(0);
  }).catchError((Object e) {
    stderr.writeln('mothc: could not push to $host:$port — $e');
    exit(70);
  });
}
