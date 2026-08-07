import 'dart:typed_data';

import 'package:analyzer/dart/analysis/utilities.dart';
import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/source/line_info.dart';

import 'errors.dart';
import 'function_compiler.dart';
import 'opcodes.dart';
import 'writer.dart';

class CompileResult {
  final Uint8List blob;
  final LineInfo lineInfo;
  CompileResult(this.blob, this.lineInfo);
}

/// Compiles one Dart source file to a .mothb blob.
///
/// M1a uses the analyzer's parser (unresolved AST): the supported language
/// subset — top-level functions, locals, numbers, bools — needs no type
/// resolution to lower correctly. Resolution arrives with classes and
/// strings in M1b (ADR-004 keeps the front end swappable).
class Compiler {
  final String path;
  final String source;

  final ConstantPool constants = ConstantPool();
  final Map<String, int> functionIndex = {};
  final List<FunctionDeclaration> _declarations = [];

  final Map<String, int> _nativeIndex = {};
  final List<NativeRef> natives = [];

  Compiler(this.path, this.source);

  CompileResult compile() {
    final parsed = parseString(
      content: source,
      path: path,
      throwIfDiagnostics: false,
    );
    final unit = parsed.unit;
    final lineInfo = parsed.lineInfo;

    final syntaxErrors = parsed.errors.where((e) => e.severity.name == 'ERROR');
    if (syntaxErrors.isNotEmpty) {
      final first = syntaxErrors.first;
      throw CompileError(first.message, first.offset);
    }

    _collectDeclarations(unit);

    if (!functionIndex.containsKey('main')) {
      throw CompileError(
        "this program has no 'main' function",
        0,
        hint: 'every moth program starts at "void main() { ... }"',
      );
    }

    final functions = <FunctionBlob>[];
    for (final decl in _declarations) {
      functions.add(FunctionCompiler(this, decl).compile());
    }

    final blob = writeBlob(
      constants: constants,
      natives: natives,
      functions: functions,
      entry: functionIndex['main']!,
    );
    return CompileResult(blob, lineInfo);
  }

  void _collectDeclarations(CompilationUnit unit) {
    for (final decl in unit.declarations) {
      if (decl is! FunctionDeclaration) {
        throw CompileError(
          'only top-level functions are supported yet',
          decl.offset,
          hint:
              'classes, mixins and top-level variables arrive in a later milestone',
        );
      }
      final name = decl.name.lexeme;
      if (kNatives.containsKey(name)) {
        throw CompileError(
          "'$name' is a built-in, so it can't also be defined here",
          decl.offset,
        );
      }
      if (functionIndex.containsKey(name)) {
        throw CompileError("'$name' is already defined", decl.offset);
      }
      functionIndex[name] = _declarations.length;
      _declarations.add(decl);
    }
  }

  /// Parameter count of a declared function, for call-site checking.
  int functionArity(int index) =>
      _declarations[index].functionExpression.parameters?.parameters.length ??
      0;

  /// Index into the blob's native table, adding the entry on first use.
  int nativeRef(String name) {
    final existing = _nativeIndex[name];
    if (existing != null) return existing;
    final argc = kNatives[name]!;
    natives.add(NativeRef(constants.addString(name), argc));
    return _nativeIndex[name] = natives.length - 1;
  }
}
