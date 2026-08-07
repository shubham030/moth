import 'package:analyzer/source/line_info.dart';

/// A compile error pinned to a source location.
///
/// Messages are written for someone learning the language: say what isn't
/// supported and what to do instead, never just "unsupported node".
class CompileError implements Exception {
  final String message;
  final int offset;
  final String? hint;

  CompileError(this.message, this.offset, {this.hint});

  String format(String path, String source, LineInfo lineInfo) {
    final loc = lineInfo.getLocation(offset);
    final line = loc.lineNumber;
    final col = loc.columnNumber;
    final lines = source.split('\n');
    final text = line <= lines.length ? lines[line - 1] : '';
    final caret = '${' ' * (col - 1)}^';
    final buf = StringBuffer()
      ..writeln('$path:$line:$col: $message')
      ..writeln('  $text')
      ..writeln('  $caret');
    if (hint != null) buf.writeln('  hint: $hint');
    return buf.toString();
  }
}
