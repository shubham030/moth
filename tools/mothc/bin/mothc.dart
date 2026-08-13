import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:analyzer/source/line_info.dart';
import 'package:mothc/src/compiler.dart';
import 'package:mothc/src/create.dart';
import 'package:mothc/src/errors.dart';
import 'package:mothc/src/push_wire.dart';
import 'package:mothc/src/serial.dart';

const _usage = '''
mothc — compile Dart to moth bytecode

usage: mothc <input.dart> [-o output.mothb] [--push HOST:PORT | --push SERIAL]
       mothc create <dir>

  create  scaffold a new moth project: one app.dart, a README, nothing else
          to install.

  --push  send the compiled program to a running host, which stops what it
          is doing and starts this instead. The display stays up.
          HOST:PORT pushes over the network; a serial device path
          (/dev/cu.usbmodemXXXX) pushes over the USB cable — no WiFi needed.
  --token ask for the board's pairing phrase and authenticate the push.
          A board provisioned with a pairing phrase refuses network pushes
          without it. Scripts can set MOTH_PUSH_TOKEN instead of the prompt.
          Serial pushes never need it — the cable is the pairing.
''';

Future<void> main(List<String> args) async {
  if (args.isEmpty || args.contains('-h') || args.contains('--help')) {
    stdout.write(_usage);
    exit(args.isEmpty ? 64 : 0);
  }

  if (args.first == 'create') {
    if (args.length != 2) {
      stderr.writeln('mothc: create needs exactly one argument, the '
          'project directory');
      exit(64);
    }
    try {
      createProject(args[1]);
    } on CreateError catch (e) {
      stderr.writeln('mothc: $e');
      exit(73);
    }
    stdout
      ..writeln('created ${args[1]}/')
      ..writeln('  app.dart    — the whole app; start in build()')
      ..writeln('  README.md   — how to run it, desktop and board')
      ..writeln()
      ..writeln('next: make ui F=${args[1]}/app.dart');
    exit(0);
  }

  String? input;
  String? output;
  String? push;
  var wantToken = false;
  for (var i = 0; i < args.length; i++) {
    if (args[i] == '-o') {
      if (i + 1 >= args.length) {
        stderr.writeln('mothc: -o needs a file name');
        exit(64);
      }
      output = args[++i];
    } else if (args[i] == '--token') {
      wantToken = true;
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
      await _push(push, result.blob, wantToken);
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
  final frame = pushFrame(blob, nonce);
  final scanner = VerdictScanner(nonce);
  for (var attempt = 1; attempt <= 3; attempt++) {
    port.writeAll(frame);
    final deadline = DateTime.now().add(const Duration(seconds: 4));
    while (DateTime.now().isBefore(deadline)) {
      final chunk = port.readAvailable();
      if (chunk.isEmpty) {
        sleep(const Duration(milliseconds: 20));
        continue;
      }
      final verdict = scanner.feed(chunk);
      if (verdict == true) {
        final ms = DateTime.now().difference(started).inMilliseconds;
        stdout.writeln('pushed over $device in ${ms}ms');
        await stdout.flush(); // exit() does not drain stdout into a pipe
        exit(0);
      }
      if (verdict == false) {
        stderr.writeln('mothc: the board rejected the program — see its log');
        await stderr.flush();
        exit(70);
      }
    }
    if (attempt < 3) {
      stderr.writeln('mothc: no reply — the board may be booting; retrying');
      sleep(const Duration(seconds: 5));
    }
  }
  stderr.writeln('mothc: no reply from $device after 3 attempts');
  await stderr.flush();
  exit(70);
}

/// One connect + frame + verdict wait. Null when the host closed without a
/// verdict — which is the receiver's "cannot verify right now, try again"
/// (moth_push_abandon), not proof of a wrong host.
Future<bool?> _tcpAttempt(
    String host, int port, Uint8List frame, VerdictScanner scanner) async {
  final socket =
      await Socket.connect(host, port, timeout: const Duration(seconds: 5));
  socket.add(frame);
  await socket.flush();
  bool? verdict;
  try {
    await for (final chunk
        in socket.timeout(const Duration(seconds: 5), onTimeout: (sink) {
      sink.close();
    })) {
      verdict = scanner.feed(chunk);
      if (verdict != null) break;
    }
  } on SocketException {
    // A peer that closes without a verdict is the null case below.
  }
  socket.destroy();
  return verdict;
}

Future<void> _pushTcp(
    String host, int port, Uint8List blob, Uint8List? key) async {
  final started = DateTime.now();
  final nonce = Random.secure().nextInt(0x100000000);
  final frame =
      key != null ? pushFrameAuthed(blob, nonce, key) : pushFrame(blob, nonce);
  final scanner = VerdictScanner(nonce);

  // The framed verdict arrives AFTER the host verified the blob — so
  // "pushed" means "verified and about to run", not merely "delivered".
  // No verdict gets retried, matching the serial path: the receiver
  // abandons (closes silently) when it cannot verify at that moment, and
  // the abandon contract IS "the sender retries".
  bool? verdict;
  for (var attempt = 1; attempt <= 3 && verdict == null; attempt++) {
    verdict = await _tcpAttempt(host, port, frame, scanner);
    if (verdict == null && attempt < 3) {
      stderr.writeln('mothc: no verdict — the host may be busy; retrying');
      await Future<void>.delayed(const Duration(seconds: 3));
    }
  }
  if (verdict == null) {
    stderr.writeln('mothc: no verdict from $host:$port after 3 attempts — '
        'the host is out of memory to verify, or not a moth host');
    await stderr.flush();
    exit(70);
  }
  if (verdict == false) {
    stderr.writeln('mothc: the host rejected the push — a bad program, or a '
        'paired board and a missing/wrong pairing phrase (--token). '
        'Its log says which.');
    await stderr.flush();
    exit(70);
  }
  final ms = DateTime.now().difference(started).inMilliseconds;
  stdout.writeln('pushed to $host:$port in ${ms}ms');
  await stdout.flush();
  exit(0);
}

/// Sends a compiled program to a listening host: "MPSH", a little-endian
/// length, then the bytes. The host verifies before running it.
///
/// The transport is picked by what the target *is*, not by how it is
/// spelled: a serial device exists on the filesystem, and testing the
/// spelling routed `COM3:` (a common Windows form, colon included) to the
/// network branch and rejected `./ttyUSB0` outright.
Future<void> _push(String target, Uint8List blob, bool wantToken) async {
  // A COM port is exactly COM + digits — a bare prefix test swallowed
  // hostnames like com.example.local:7621.
  if (Platform.isWindows &&
      RegExp(r'^COM\d+$', caseSensitive: false).hasMatch(target)) {
    stderr.writeln('mothc: serial push is not implemented on Windows yet — '
        'push over WiFi (HOST:PORT) instead');
    exit(74);
  }
  if (File(target).existsSync()) {
    // No token on serial, ever — the receiver never checks one there, and
    // prompting would teach people to type the phrase where it isn't needed.
    try {
      await _pushSerial(target, blob);
    } on SerialException catch (e) {
      stderr.writeln('mothc: serial push failed — $e');
      exit(74);
    }
    return;
  }

  // The phrase comes from the prompt (--token) or the environment; it is
  // reduced to its derived key immediately and never stored or echoed. A
  // paired board answers a plain push with a real MPRJ — after draining
  // the frame (replying mid-stream draws an RST that eats the reply), so
  // a forgotten token costs one full transfer, then fails cleanly rather
  // than timing out.
  Uint8List? key;
  // MOTH_PUSH_KEY carries the DERIVED key (64 hex chars) and skips the
  // ~2s KDF — for scripts that push in a loop. Deriving it once:
  //   python3 -c "import hashlib; print(hashlib.pbkdf2_hmac('sha256',
  //     b'PHRASE', b'moth-push-v1', 600000).hex())"
  // An explicit --token beats the ambient environment; and a key that is
  // set but unusable is a hard error, not a fall-through — silently
  // pushing unauthenticated because of a stray newline in a keyfile is
  // the sender-side version of failing open.
  final envKey = Platform.environment['MOTH_PUSH_KEY'];
  final envToken = Platform.environment['MOTH_PUSH_TOKEN'];
  if (!wantToken && envKey != null) {
    if (!RegExp(r'^[0-9a-fA-F]{64}$').hasMatch(envKey)) {
      stderr.writeln('mothc: MOTH_PUSH_KEY is set but is not 64 hex '
          'characters (got ${envKey.length}) — refusing to push without it');
      exit(64);
    }
    key = Uint8List.fromList([
      for (var i = 0; i < 64; i += 2)
        int.parse(envKey.substring(i, i + 2), radix: 16),
    ]);
  } else if (wantToken) {
    stderr.write('pairing phrase: ');
    final hadEcho = stdin.echoMode;
    stdin.echoMode = false;
    final phrase = stdin.readLineSync() ?? '';
    stdin.echoMode = hadEcho;
    stderr.writeln();
    if (phrase.isEmpty) {
      stderr.writeln('mothc: an empty pairing phrase cannot match a paired '
          'board');
      exit(64);
    }
    key = keyFromPassphrase(phrase);
  } else if (envToken != null && envToken.isNotEmpty) {
    key = keyFromPassphrase(envToken);
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
    await _pushTcp(host, port, blob, key);
  } on SocketException catch (e) {
    stderr.writeln('mothc: could not push to $host:$port — ${e.message}');
    exit(70);
  }
}
