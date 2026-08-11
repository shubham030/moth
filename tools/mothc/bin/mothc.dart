import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:analyzer/source/line_info.dart';
import 'package:mothc/src/compiler.dart';
import 'package:mothc/src/errors.dart';

const _usage = '''
mothc — compile Dart to moth bytecode

usage: mothc <input.dart> [-o output.mothb] [--push HOST:PORT]

  --push  send the compiled program to a running host, which stops what it
          is doing and starts this instead. The display stays up.
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

/// Sends a compiled program to a listening host: "MPSH", a little-endian
/// length, then the bytes. The host verifies before running it.
void _push(String target, Uint8List blob) {
  final colon = target.lastIndexOf(':');
  if (colon < 0) {
    stderr.writeln('mothc: --push wants HOST:PORT');
    exit(64);
  }
  final host = target.substring(0, colon);
  final port = int.tryParse(target.substring(colon + 1));
  if (port == null) {
    stderr.writeln('mothc: --push wants HOST:PORT');
    exit(64);
  }

  final header = BytesBuilder()
    ..add(ascii.encode('MPSH'))
    ..add([
      blob.length & 0xFF,
      (blob.length >> 8) & 0xFF,
      (blob.length >> 16) & 0xFF,
      (blob.length >> 24) & 0xFF,
    ]);

  final started = DateTime.now();
  Socket.connect(host, port, timeout: const Duration(seconds: 5))
      .then((socket) async {
    socket.add(header.toBytes());
    socket.add(blob);
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
