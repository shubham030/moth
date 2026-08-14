// Two things that silently rot, made executable.
//
// 1. package:moth declares every built-in `external` so the analyzer knows
//    it. mothc has its own table. Nothing structurally ties them together,
//    so a native added to one and not the other gives either a compile
//    error the editor never predicted, or an editor promise the compiler
//    refuses. This asserts they describe the same set, with the same arity.
//
// 2. The docs quote blink's exact byte count in seven places. The v6 assets
//    section moved it by two bytes and every one of them went stale at
//    once. The number is now checked against a real compile.
import 'dart:io';

import 'package:mothc/src/opcodes.dart';
import 'package:test/test.dart';

void main() {
  final repoRoot = Directory.current.path.endsWith('tools/mothc')
      ? Directory.current.parent.parent.path
      : Directory.current.path;

  test('every built-in is declared for the analyzer, and vice versa', () {
    final src = File('$repoRoot/packages/moth/lib/natives.dart');
    expect(src.existsSync(), isTrue, reason: 'natives.dart should exist');
    final text = src.readAsStringSync();

    // external <type> name(<params>);
    final declared = <String, int>{};
    for (final m in RegExp(r'external\s+[\w<>?]+\s+(\w+)\s*\(([^)]*)\)\s*;')
        .allMatches(text)) {
      final name = m.group(1)!;
      final params = m.group(2)!.trim();
      declared[name] = params.isEmpty ? 0 : params.split(',').length;
    }
    expect(declared, isNotEmpty, reason: 'the regex should find declarations');

    // `print` is dart:core's; moth resolves it as a native but must not
    // redeclare it, so it is the one documented exception.
    final expected = Map<String, int>.from(kNatives)..remove('print');

    expect(declared.keys.toSet(), expected.keys.toSet(),
        reason: 'natives.dart and kNatives must cover the same built-ins');
    for (final name in expected.keys) {
      expect(declared[name], expected[name],
          reason: "'$name' has a different arity in natives.dart than in "
              'mothc — the analyzer would accept calls the compiler rejects');
    }
  });

  test('the load-bearing signatures read exactly as the C implements them',
      () {
    // Arity alone cannot catch a num->double or int->bool drift, and those
    // are the changes that silently break valid programs: double would
    // reject the integer literals want_num accepts, bool would hide
    // uartRead's -1 sentinel. Pin the exact lines for the ones with a
    // wrong-type failure mode; a deliberate change updates both sides.
    final text = File('$repoRoot/packages/moth/lib/natives.dart')
        .readAsStringSync();
    for (final line in [
      'external void uiSetNum(int node, int prop, num value);',
      'external int uiAnimate(',
      'external int uartRead(int port);',
      'external void uartWrite(int port, int byte);',
      'external void digitalWrite(int pin, bool value);',
      'external bool digitalRead(int pin);',
      'external bool i2cPing(int addr);',
      'external int i2cReadReg(int addr, int reg);',
      'external double uiEventValue();',
      'external bool uiCommit();',
    ]) {
      expect(text, contains(line),
          reason: 'natives.dart no longer declares "$line" — if the change '
              'is deliberate, verify it against the C implementation and '
              'update this pin');
    }
  });

  test('the byte counts quoted in the docs are the real ones', () {
    // Every small program the docs quote a size for. A three-digit "N
    // bytes" claim in those pages must equal one of these; the v6 assets
    // section moved both by two and every claim went stale at once.
    final sizes = <String, int>{};
    for (final rel in ['examples/blink.dart', 'examples/board_demo.dart']) {
      final out =
          '${Directory.systemTemp.createTempSync('moth_size').path}/b.mothb';
      final r = Process.runSync('dart', [
        'run',
        '$repoRoot/tools/mothc/bin/mothc.dart',
        '$repoRoot/$rel',
        '-o',
        out,
      ]);
      expect(r.exitCode, 0, reason: '${r.stdout}${r.stderr}');
      sizes[rel] = File(out).lengthSync();
    }

    // Every place the number is quoted, so one compile checks them all.
    for (final rel in [
      'README.md',
      'docs/getting-started.md',
      'docs/how-it-works.md',
      'docs/language.md',
      'docs/ARCHITECTURE.md',
      'website/src/pages/index.tsx',
    ]) {
      final f = File('$repoRoot/$rel');
      if (!f.existsSync()) continue;
      for (final m
          in RegExp(r'(\d+) bytes').allMatches(f.readAsStringSync())) {
        final quoted = int.parse(m.group(1)!);
        // Only blink-sized claims; other numbers (KB figures, widget app
        // sizes) are not this test's business.
        if (quoted > 100 && quoted < 1000) {
          expect(sizes.values, contains(quoted),
              reason: '$rel quotes $quoted bytes, but the programs the docs '
                  'describe now compile to $sizes');
        }
      }
    }
  });
}
