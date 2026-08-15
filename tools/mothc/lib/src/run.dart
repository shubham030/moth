/// `mothc run` — compile, put the program on a device, stay attached.
///
/// The shape is flutter run's: launch, stream the program's output, and a
/// keystroke re-deploys. The honest difference is named honestly: `r` is a
/// hot RESTART — the program is recompiled, pushed (~150ms over serial),
/// and starts from `main` with fresh state. State-preserving reload is on
/// the roadmap; calling this "reload" would promise it early.
///
/// On a board the attach session owns the serial port for its whole life:
/// pushes go through the same open port the console is read from, so
/// there is no port contention and no reset-on-reopen between restarts.
/// On the simulator, mothsim is launched with --listen and restarts arrive
/// as local TCP pushes.
///
/// A compile error during `r` is shown and the session stays attached —
/// the running program keeps running, exactly like flutter run.
library;

import 'dart:async';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:analyzer/source/line_info.dart';

import 'compiler.dart';

import 'devices.dart';
import 'errors.dart';
import 'push_wire.dart';
import 'serial.dart';

/// Raw-mode guard. hasTerminal alone is not enough — under some parents
/// (a backgrounded shell, a CI runner) it reports true while the mode
/// getters throw, so every touch is defensive. When the terminal cannot
/// be configured, keys simply do not work and the session still streams.
class _RawTerminal {
  bool? _echoWas, _lineWas;

  void enter() {
    try {
      _echoWas = stdin.echoMode;
      _lineWas = stdin.lineMode;
      stdin.lineMode = false;
      stdin.echoMode = false;
    } on StdinException {
      _echoWas = null;
      _lineWas = null;
    }
  }

  void leave() {
    if (_echoWas == null) return;
    try {
      stdin.echoMode = _echoWas!;
      stdin.lineMode = _lineWas!;
    } on StdinException {
      // The terminal went away with the session; nothing to restore.
    }
  }
}

/// Compiles [input]; on error prints it formatted and returns null rather
/// than exiting, so an attached session survives a typo.
Uint8List? compileQuietly(String input) {
  final source = File(input).readAsStringSync();
  final compiler = Compiler(input, source);
  try {
    return compiler.compile().blob;
  } on CompileError catch (e) {
    final unit = compiler.currentUnit;
    if (unit != null) {
      stderr.write(e.format(unit.path, unit.source, unit.lineInfo));
    } else {
      stderr.write(e.format(input, source, LineInfo.fromContent(source)));
    }
    return null;
  }
}

/// Removes push verdict frames (MPOK/MPRJ + nonce) from bytes headed for
/// the user's terminal. The board sends each verdict three times because
/// the console is shared; the push logic consumes what it needs from its
/// own scan, but the display would otherwise show the copies as noise.
/// Carries a partial match across chunks.
class VerdictDisplayFilter {
  final _carry = <int>[];

  /// Nonces this session actually issued. A frame is only stripped when its
  /// trailing four bytes name one of them — a program that print()s the
  /// literal text "MPOK" must not lose eight bytes of its own output.
  final _nonces = <int>{};

  void expectNonce(int nonce) => _nonces.add(nonce);

  /// Releases a held partial match. Call when the wire goes quiet: a held
  /// "MP" tail would otherwise be withheld forever on a program whose last
  /// output happens to end on a verdict prefix.
  Uint8List flush() {
    final out = Uint8List.fromList(_carry);
    _carry.clear();
    return out;
  }

  Uint8List filter(Uint8List chunk) {
    final data = <int>[..._carry, ...chunk];
    _carry.clear();
    final out = <int>[];
    var i = 0;
    while (i < data.length) {
      final remaining = data.length - i;
      final isStart = data[i] == 0x4D /* M */ &&
          remaining >= 2 &&
          data[i + 1] == 0x50 /* P */;
      if (isStart) {
        if (remaining < 8) {
          // Could be a frame split across reads — hold it back.
          final maybe = data.sublist(i);
          if (_looksLikeVerdictPrefix(maybe)) {
            _carry.addAll(maybe);
            break;
          }
        } else if (_isVerdict(data, i) && _nonces.contains(_nonceAt(data, i))) {
          i += 8; // drop the whole frame
          continue;
        }
      }
      out.add(data[i]);
      i++;
    }
    return Uint8List.fromList(out);
  }

  static bool _isVerdict(List<int> d, int i) {
    final cc = String.fromCharCodes(d.sublist(i, i + 4));
    return cc == 'MPOK' || cc == 'MPRJ';
  }

  static int _nonceAt(List<int> d, int i) =>
      d[i + 4] | (d[i + 5] << 8) | (d[i + 6] << 16) | (d[i + 7] << 24);

  static bool _looksLikeVerdictPrefix(List<int> d) {
    const ok = [0x4D, 0x50, 0x4F, 0x4B]; // MPOK
    const rj = [0x4D, 0x50, 0x52, 0x4A]; // MPRJ
    for (var j = 0; j < d.length && j < 4; j++) {
      if (d[j] != ok[j] && d[j] != rj[j]) return false;
    }
    return d.length < 8;
  }
}

/// SIGTERM/SIGHUP land outside any finally, which would leave the user's
/// terminal raw — the one failure a keystroke cannot fix, in a session
/// built on keystrokes. flutter run installs handlers for exactly this.
List<StreamSubscription<ProcessSignal>> _onSignals(void Function() cleanup) {
  return [
    for (final sig in [ProcessSignal.sigterm, ProcessSignal.sighup])
      sig.watch().listen((_) {
        cleanup();
        exit(143);
      }),
  ];
}

void _banner(String input, String target) {
  stdout
    ..writeln('Launching $input on $target')
    ..writeln();
}

void _keysHelp() {
  stdout.writeln(
      'r  hot restart (recompile + push; state resets)   h  this help   '
      'q  quit');
}

/// Pushes [blob] through an already-open serial [port], consuming only the
/// bytes its verdict scan needs; everything else (boot logs, prints) goes
/// through [display]. True on MPOK, false on MPRJ, null on silence.
bool? pushOverOpenPort(
    SerialPort port, Uint8List blob, VerdictDisplayFilter display) {
  final nonce = Random.secure().nextInt(0x100000000);
  display.expectNonce(nonce);
  final frame = pushFrame(blob, nonce);
  final scanner = VerdictScanner(nonce);
  for (var attempt = 1; attempt <= 2; attempt++) {
    port.writeAll(frame);
    final deadline = DateTime.now().add(const Duration(seconds: 4));
    while (DateTime.now().isBefore(deadline)) {
      final chunk = port.readAvailable();
      if (chunk.isEmpty) {
        sleep(const Duration(milliseconds: 15));
        continue;
      }
      final shown = display.filter(chunk);
      if (shown.isNotEmpty) stdout.add(shown);
      final verdict = scanner.feed(chunk);
      if (verdict != null) return verdict;
    }
  }
  return null;
}

/// The attached loop for a serial board.
Future<int> runOnBoard(String input, Device device) async {
  var blob = compileQuietly(input);
  if (blob == null) return 65;

  final SerialPort port;
  try {
    port = SerialPort.open(device.id);
  } on SerialException catch (e) {
    stderr.writeln('mothc: $e');
    return 74;
  }

  _banner(input, device.id);
  final display = VerdictDisplayFilter();

  var started = DateTime.now();
  var verdict = pushOverOpenPort(port, blob, display);
  if (verdict != true) {
    port.close();
    stderr.writeln(verdict == false
        ? 'mothc: the board rejected the program — its log says why'
        : 'mothc: no reply from ${device.id} — is a moth firmware running?');
    return 70;
  }
  stdout.writeln(
      'pushed in ${DateTime.now().difference(started).inMilliseconds}ms');
  _keysHelp();

  final term = _RawTerminal()..enter();
  final keys = <int>[];
  final sub = stdin.listen(keys.addAll);
  final signals = _onSignals(() {
    term.leave();
    port.close();
  });

  var exitCode = 0;
  try {
    while (true) {
      final chunk = port.readAvailable();
      if (chunk.isNotEmpty) {
        final shown = display.filter(chunk);
        if (shown.isNotEmpty) stdout.add(shown);
      } else {
        final held = display.flush();
        if (held.isNotEmpty) stdout.add(held);
      }

      while (keys.isNotEmpty) {
        final k = keys.removeAt(0);
        if (k == 0x71 /* q */ || k == 0x03 /* ^C */) return exitCode;
        if (k == 0x68 /* h */) _keysHelp();
        if (k == 0x72 /* r */ || k == 0x52 /* R */) {
          stdout.writeln('\nRestarting...');
          final next = compileQuietly(input);
          if (next == null) {
            stdout.writeln('compile failed — still running the old program');
            continue;
          }
          blob = next;
          started = DateTime.now();
          final v = pushOverOpenPort(port, blob, display);
          if (v == true) {
            final ms = DateTime.now().difference(started).inMilliseconds;
            stdout.writeln('Restarted in ${ms}ms (state resets)');
          } else {
            stdout.writeln(v == false
                ? 'the board rejected the program — fix and press r again'
                : 'no reply — press r to retry');
          }
        }
      }
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
  } finally {
    for (final s in signals) {
      await s.cancel();
    }
    await sub.cancel();
    term.leave();
    port.close();
  }
}

/// The attached loop for the simulator: mothsim is a child process
/// listening for pushes on localhost.
Future<int> runOnSim(String input, Device device) async {
  final blob = compileQuietly(input);
  if (blob == null) return 65;

  final simPath = findMothsim();
  if (simPath == null) {
    stderr.writeln('mothc: mothsim is not built — run: make vm');
    return 69;
  }

  // An ephemeral port the OS says is free right now.
  final probe = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
  final port = probe.port;
  await probe.close();

  final dir = Directory.systemTemp.createTempSync('moth-run');
  final blobPath = '${dir.path}/app.mothb';
  File(blobPath).writeAsBytesSync(blob);

  _banner(input, 'the simulator');
  final proc = await Process.start(simPath, [blobPath, '--listen', '$port']);
  proc.stdout.listen(stdout.add);
  proc.stderr.listen(stderr.add);
  _keysHelp();

  final term = _RawTerminal()..enter();
  final keys = <int>[];
  final sub = stdin.listen(keys.addAll);
  final signals = _onSignals(() {
    term.leave();
    proc.kill();
  });
  var done = false;
  unawaited(proc.exitCode.then((_) => done = true));

  try {
    while (!done) {
      while (keys.isNotEmpty) {
        final k = keys.removeAt(0);
        if (k == 0x71 || k == 0x03) {
          proc.kill();
          return 0;
        }
        if (k == 0x68) _keysHelp();
        if (k == 0x72 || k == 0x52) {
          stdout.writeln('\nRestarting...');
          final next = compileQuietly(input);
          if (next == null) {
            stdout.writeln('compile failed — still running the old program');
            continue;
          }
          final started = DateTime.now();
          final ok = await _pushTcpOnce('127.0.0.1', port, next);
          if (ok == true) {
            final ms = DateTime.now().difference(started).inMilliseconds;
            stdout.writeln('Restarted in ${ms}ms (state resets)');
          } else {
            stdout.writeln('push to the simulator failed — press r to retry');
          }
        }
      }
      await Future<void>.delayed(const Duration(milliseconds: 30));
    }
    stdout.writeln('the simulator window closed');
    return await proc.exitCode;
  } finally {
    for (final s in signals) {
      await s.cancel();
    }
    await sub.cancel();
    term.leave();
    if (!done) proc.kill();
    try {
      dir.deleteSync(recursive: true);
    } on FileSystemException {
      // A leftover temp dir is not worth failing the session over.
    }
  }
}

Future<bool?> _pushTcpOnce(String host, int port, Uint8List blob) async {
  final nonce = Random.secure().nextInt(0x100000000);
  final frame = pushFrame(blob, nonce);
  final scanner = VerdictScanner(nonce);
  try {
    final socket =
        await Socket.connect(host, port, timeout: const Duration(seconds: 3));
    socket.add(frame);
    await socket.flush();
    bool? verdict;
    await for (final chunk
        in socket.timeout(const Duration(seconds: 4), onTimeout: (s) {
      s.close();
    })) {
      verdict = scanner.feed(chunk);
      if (verdict != null) break;
    }
    socket.destroy();
    return verdict;
  } on SocketException {
    return null;
  }
}
