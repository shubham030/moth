import 'dart:io';

import 'package:analyzer/source/line_info.dart';
import 'package:mothc/src/compiler.dart';
import 'package:mothc/src/errors.dart';

const _usage = '''
mothc — compile Dart to moth bytecode

usage: mothc <input.dart> [-o output.mothb]
''';

void main(List<String> args) {
  if (args.isEmpty || args.contains('-h') || args.contains('--help')) {
    stdout.write(_usage);
    exit(args.isEmpty ? 64 : 0);
  }

  String? input;
  String? output;
  for (var i = 0; i < args.length; i++) {
    if (args[i] == '-o') {
      if (i + 1 >= args.length) {
        stderr.writeln('mothc: -o needs a file name');
        exit(64);
      }
      output = args[++i];
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
  } on CompileError catch (e) {
    stderr.write(e.format(input, source, LineInfo.fromContent(source)));
    exit(65);
  }
}
