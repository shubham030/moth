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

  /// Top-level variables get one slot each, initialized before `main` runs.
  final Map<String, int> globalIndex = {};
  final List<(int, Expression)> globalInits = [];

  /// Classes, in declaration order. `classIndex` maps name to position.
  final Map<String, int> classIndex = {};
  final List<ClassDeclaration> classDeclarations = [];
  final List<List<String>> classFields = [];

  /// class index -> function index of its constructor, absent when the class
  /// has none (a fresh instance with null fields is the whole construction).
  final Map<int, int> classCtorIndex = {};

  /// class index -> its methods' reserved function indices, in declaration
  /// order. Reserved before any body is compiled so that a call site can
  /// reference a constructor declared later in the file.
  final List<List<(String, int)>> classMethodSlots = [];

  /// class index -> field initializers (`int value = 0;`), which run at the
  /// start of construction, before the constructor body.
  final List<List<(String, Expression)>> classFieldInits = [];

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

    _reserveMemberIndices();

    final functions = <FunctionBlob>[];
    for (final decl in _declarations) {
      functions.add(FunctionCompiler(this, decl).compile());
    }

    // Classes: constructors and methods become ordinary functions whose
    // slot 0 is the receiver. The class table records where they landed.
    final classes = <ClassBlob>[];
    for (var i = 0; i < classDeclarations.length; i++) {
      classes.add(_compileClass(i, functions));
    }

    // Top-level initializers become a synthetic function the VM runs first.
    var init = noInit;
    if (globalInits.isNotEmpty) {
      init = functions.length;
      functions.add(FunctionCompiler.globalsInit(this, globalInits).compile());
    }

    final blob = writeBlob(
      constants: constants,
      natives: natives,
      functions: functions,
      entry: functionIndex['main']!,
      globalCount: globalIndex.length,
      init: init,
      classes: classes,
    );
    return CompileResult(blob, lineInfo);
  }

  void _collectDeclarations(CompilationUnit unit) {
    for (final decl in unit.declarations) {
      if (decl is TopLevelVariableDeclaration) {
        _collectGlobals(decl);
        continue;
      }
      if (decl is ClassDeclaration) {
        _collectClass(decl);
        continue;
      }
      if (decl is! FunctionDeclaration) {
        throw CompileError(
          'only classes, functions and top-level variables are supported yet',
          decl.offset,
          hint: 'mixins, enums and extensions arrive later',
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

  /// Constructors and methods are appended after the top-level functions, so
  /// their indices are predictable before any body is lowered.
  void _reserveMemberIndices() {
    var next = _declarations.length;
    for (var i = 0; i < classDeclarations.length; i++) {
      final decl = classDeclarations[i];
      // A class with field initializers needs a constructor even if it
      // declares none, so those initializers have somewhere to run.
      final hasCtor = decl.members.any((m) => m is ConstructorDeclaration);
      if (hasCtor || classFieldInits[i].isNotEmpty) {
        classCtorIndex[i] = next++;
      }
      final slots = <(String, int)>[];
      for (final member in decl.members) {
        if (member is MethodDeclaration) {
          slots.add((member.name.lexeme, next++));
        }
      }
      classMethodSlots.add(slots);
    }
  }

  ClassBlob _compileClass(int index, List<FunctionBlob> functions) {
    final decl = classDeclarations[index];
    final fields = classFields[index];
    final methodNames = <String>[
      for (final m in decl.members)
        if (m is MethodDeclaration) m.name.lexeme,
    ];

    var ctor = noCtor;
    final methods = <(int, int)>[];
    final fieldInits = classFieldInits[index];
    final declaredCtor =
        decl.members.whereType<ConstructorDeclaration>().firstOrNull;

    // The constructor is emitted first so its index is independent of where
    // it appears among the members (and exists even when synthesized).
    if (classCtorIndex.containsKey(index)) {
      ctor = classCtorIndex[index]!;
      assert(
          ctor == functions.length, 'reserved index must match append order');
      functions.add(
        FunctionCompiler.member(this, fields, methodNames, isConstructor: true)
            .compileMember(
          name: '${decl.name.lexeme}()',
          params: declaredCtor?.parameters.parameters ?? <FormalParameter>[],
          body: declaredCtor?.body,
          offset: declaredCtor?.offset ?? decl.offset,
          fieldInits: fieldInits,
        ),
      );
    }

    for (final member in decl.members) {
      if (member is FieldDeclaration) continue; // collected already

      if (member is ConstructorDeclaration) {
        if (member.name != null) {
          throw CompileError(
            'named constructors are not supported yet',
            member.offset,
          );
        }
        if (member != declaredCtor) {
          throw CompileError(
              'a class may have one constructor for now', member.offset);
        }
        if (member.initializers.isNotEmpty) {
          throw CompileError(
            'constructor initializer lists are not supported yet',
            member.offset,
            hint: 'assign in the body, or use "this.field" parameters',
          );
        }
        continue; // already emitted above
      }

      if (member is MethodDeclaration) {
        if (member.isStatic) {
          throw CompileError(
              'static methods are not supported yet', member.offset);
        }
        // Accepting this silently would be worse than rejecting it: print()
        // and '$obj' would keep saying "Instance" while real Dart calls the
        // override, and the program would differ from Dart without saying so.
        if (member.name.lexeme == 'toString') {
          throw CompileError(
            'toString() overrides are not supported yet',
            member.offset,
            hint: 'give it another name and call it explicitly, '
                'e.g. "print(thing.describe())"',
          );
        }
        if (member.isGetter || member.isSetter) {
          throw CompileError(
            'getters and setters are not supported yet',
            member.offset,
            hint: 'use a plain method for now',
          );
        }
        final reserved = classMethodSlots[index]
            .firstWhere((s) => s.$1 == member.name.lexeme)
            .$2;
        assert(reserved == functions.length,
            'reserved index must match append order');
        methods.add((constants.addString(member.name.lexeme), reserved));
        functions.add(
          FunctionCompiler.member(this, fields, methodNames,
                  isConstructor: false)
              .compileMember(
            name: '${decl.name.lexeme}.${member.name.lexeme}',
            params: member.parameters?.parameters ?? const [],
            body: member.body,
            offset: member.offset,
          ),
        );
        continue;
      }

      throw CompileError(
          'this class member is not supported yet', member.offset);
    }

    return ClassBlob(
      constants.addString(decl.name.lexeme),
      [for (final f in fields) constants.addString(f)],
      methods,
      ctor,
    );
  }

  void _collectClass(ClassDeclaration decl) {
    final name = decl.name.lexeme;
    if (classIndex.containsKey(name) ||
        functionIndex.containsKey(name) ||
        globalIndex.containsKey(name)) {
      throw CompileError("'$name' is already defined", decl.offset);
    }
    if (decl.extendsClause != null || decl.withClause != null) {
      throw CompileError(
        'inheritance is not supported yet',
        decl.offset,
        hint: 'classes stand alone for now — compose instead of extending',
      );
    }

    final fields = <String>[];
    final inits = <(String, Expression)>[];
    for (final member in decl.members) {
      if (member is FieldDeclaration) {
        if (member.isStatic) {
          throw CompileError(
              'static fields are not supported yet', member.offset);
        }
        for (final v in member.fields.variables) {
          fields.add(v.name.lexeme);
          if (v.initializer != null) {
            inits.add((v.name.lexeme, v.initializer!));
          }
        }
      }
    }

    if (fields.length > 255) {
      throw CompileError(
        'a class may have at most 255 fields',
        decl.offset,
      );
    }
    final seen = <String>{};
    for (final member in decl.members) {
      if (member is MethodDeclaration) {
        if (!seen.add(member.name.lexeme)) {
          throw CompileError(
            "'${member.name.lexeme}' is declared twice in this class",
            member.offset,
          );
        }
      }
    }

    classIndex[name] = classDeclarations.length;
    classDeclarations.add(decl);
    classFields.add(fields);
    classFieldInits.add(inits);
  }

  void _collectGlobals(TopLevelVariableDeclaration decl) {
    for (final v in decl.variables.variables) {
      final name = v.name.lexeme;
      if (kNatives.containsKey(name) || functionIndex.containsKey(name)) {
        throw CompileError("'$name' is already defined", v.offset);
      }
      if (globalIndex.containsKey(name)) {
        throw CompileError("'$name' is already defined", v.offset);
      }
      if (globalIndex.length >= 0xFFFF) {
        throw CompileError('too many top-level variables', v.offset);
      }
      final slot = globalIndex.length;
      globalIndex[name] = slot;
      if (v.initializer != null) {
        globalInits.add((slot, v.initializer!));
      }
    }
  }

  /// Parameter count of a class's constructor, excluding the receiver.
  /// Null when the class declares none.
  int? classCtorArity(int classIdx) {
    for (final member in classDeclarations[classIdx].members) {
      if (member is ConstructorDeclaration) {
        return member.parameters.parameters.length;
      }
    }
    return null;
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
