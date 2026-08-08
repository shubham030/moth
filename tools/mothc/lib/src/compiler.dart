import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:analyzer/dart/analysis/utilities.dart';
import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/source/line_info.dart';
import 'package:path/path.dart' as p;

import 'errors.dart';
import 'function_compiler.dart';
import 'opcodes.dart';
import 'writer.dart';

class CompileResult {
  final Uint8List blob;
  final LineInfo lineInfo;
  CompileResult(this.blob, this.lineInfo);
}

/// One parsed file. Errors are reported against whichever unit was being
/// processed, so a message from an imported file points at that file.
class SourceUnit {
  final String path;
  final String source;
  final LineInfo lineInfo;
  SourceUnit(this.path, this.source, this.lineInfo);
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

  /// class index -> superclass index, or null. Inheritance is flattened at
  /// compile time: a subclass carries its superclass's fields and methods,
  /// with its own overriding by name. Because dispatch is by name at run
  /// time, an inherited method calling an overridden one lands on the
  /// override automatically — virtual dispatch with no runtime machinery.
  final List<int?> classSuper = [];
  final List<String?> _pendingSuperNames = [];

  /// Reserved slots come first (top-level functions, then class members);
  /// lambdas are appended as they are compiled.
  final List<FunctionBlob?> functions = [];
  var _reservedCount = 0;

  /// Appends a function compiled on the fly (a lambda, or the globals
  /// initializer) and returns its index.
  int addFunction(FunctionBlob blob) {
    functions.add(blob);
    return functions.length - 1;
  }

  final Map<String, int> _nativeIndex = {};
  final List<NativeRef> natives = [];

  Compiler(this.path, this.source);

  /// The file currently being processed, so an error can be shown against
  /// the right source even when it came from an import.
  SourceUnit? currentUnit;
  final List<SourceUnit> _units = [];
  final Set<String> _loaded = {};

  /// Parses [unitPath], then its imports, depth first. A file already loaded
  /// is skipped, so import cycles terminate.
  void _loadUnit(String unitPath, String contents, int fromOffset) {
    // Key on the file's identity, not on how it was spelled. A case-insensitive
    // filesystem makes 'a.dart' and 'A.dart' the same file, and a symlink gives
    // two paths to one source; keying on the string loads it twice and then
    // rejects the program for redeclaring everything in it.
    var identity = p.normalize(p.absolute(unitPath));
    try {
      identity = File(unitPath).resolveSymbolicLinksSync();
    } on FileSystemException {
      // Missing files are reported by the caller, with a better message.
    }
    if (!_loaded.add(identity)) return;

    final parsed = parseString(
        content: contents, path: unitPath, throwIfDiagnostics: false);
    final unit = SourceUnit(unitPath, contents, parsed.lineInfo);
    _units.add(unit);
    currentUnit = unit;

    final syntaxErrors = parsed.errors.where((e) => e.severity.name == 'ERROR');
    if (syntaxErrors.isNotEmpty) {
      final first = syntaxErrors.first;
      throw CompileError(first.message, first.offset);
    }

    // Imports first, so a superclass or function from another file is known
    // before this file's declarations are collected.
    for (final directive in parsed.unit.directives) {
      if (directive is ImportDirective) {
        final target = directive.uri.stringValue;
        if (target == null) {
          throw CompileError('this import is not a plain string',
              directive.offset);
        }
        if (target.startsWith('dart:')) {
          throw CompileError(
            "'$target' is not available on a microcontroller",
            directive.offset,
            hint: "moth's own libraries live under package:moth — "
                "try import 'package:moth/moth.dart';",
          );
        }

        final String resolved;
        if (target.startsWith('package:')) {
          resolved = _resolvePackageUri(target, directive.uri.offset);
        } else if (target.contains(':')) {
          throw CompileError(
            "'$target' is not a kind of import moth understands",
            directive.offset,
            hint: "use a relative path, or package:<name>/<file>.dart",
          );
        } else {
          resolved = p.join(p.dirname(unitPath), target);
        }

        final file = File(resolved);
        if (!file.existsSync()) {
          throw CompileError("cannot find '$target'", directive.uri.offset);
        }
        _loadUnit(resolved, file.readAsStringSync(), directive.offset);
        currentUnit = unit; // restore after the nested load
      } else if (directive is! LibraryDirective) {
        throw CompileError(
          'only import directives are supported',
          directive.offset,
        );
      }
    }

    _pendingUnits.add((unit, parsed.unit));
  }

  final List<(SourceUnit, CompilationUnit)> _pendingUnits = [];
  final Map<int, SourceUnit> _declarationUnit = {};
  final Map<int, SourceUnit> _classUnit = {};

  CompileResult compile() {
    _loadUnit(path, source, 0);

    for (final (unit, ast) in _pendingUnits) {
      currentUnit = unit;
      _collectDeclarations(ast);
    }
    final lineInfo = _units.first.lineInfo;

    if (!functionIndex.containsKey('main')) {
      throw CompileError(
        "this program has no 'main' function",
        0,
        hint: 'every moth program starts at "void main() { ... }"',
      );
    }

    _resolveInheritance();
    _reserveMemberIndices();

    // Slots for the top-level functions and every class member are reserved
    // up front; lambdas are appended past them as they are encountered.
    functions.addAll(List<FunctionBlob?>.filled(_reservedCount, null));

    for (var i = 0; i < _declarations.length; i++) {
      currentUnit = _declarationUnit[i] ?? currentUnit;
      functions[i] = FunctionCompiler(this, _declarations[i]).compile();
    }

    // Classes: constructors and methods become ordinary functions whose
    // slot 0 is the receiver. The class table records where they landed.
    final classes = <ClassBlob>[];
    for (var i = 0; i < classDeclarations.length; i++) {
      currentUnit = _classUnit[i] ?? currentUnit;
      classes.add(_compileClass(i, functions));
    }

    // Top-level initializers become a synthetic function the VM runs first.
    var init = noInit;
    if (globalInits.isNotEmpty) {
      init = addFunction(
          FunctionCompiler.globalsInit(this, globalInits).compile());
    }

    for (var i = 0; i < functions.length; i++) {
      if (functions[i] == null) {
        throw StateError('function slot $i was reserved but never filled');
      }
    }

    final blob = writeBlob(
      constants: constants,
      natives: natives,
      functions: [for (final f in functions) f!],
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
      _declarationUnit[_declarations.length] = currentUnit!;
      _declarations.add(decl);
    }
  }

  void _resolveInheritance() {
    for (var i = 0; i < _pendingSuperNames.length; i++) {
      final superName = _pendingSuperNames[i];
      if (superName == null) continue;
      final superIdx = classIndex[superName];
      if (superIdx == null) {
        throw CompileError(
          "'$superName' is not a class in this file",
          classDeclarations[i].offset,
          hint:
              'moth has no imports yet, so a superclass must be declared here',
        );
      }
      if (superIdx == i) {
        throw CompileError(
          'a class cannot extend itself',
          classDeclarations[i].offset,
        );
      }
      classSuper[i] = superIdx;
    }

    // A cycle would make the field/method walks below run forever.
    for (var i = 0; i < classSuper.length; i++) {
      var seen = <int>{i};
      var walk = classSuper[i];
      while (walk != null) {
        if (!seen.add(walk)) {
          throw CompileError(
            'these classes extend each other in a circle',
            classDeclarations[i].offset,
          );
        }
        walk = classSuper[walk];
      }
    }

    // A superclass constructor body cannot be chained to yet, so reject the
    // combination rather than silently skipping it.
    for (var i = 0; i < classSuper.length; i++) {
      final superIdx = classSuper[i];
      if (superIdx == null) continue;
      final superCtor = classDeclarations[superIdx]
          .members
          .whereType<ConstructorDeclaration>()
          .firstOrNull;
      if (superCtor != null) {
        throw CompileError(
          'extending a class that declares a constructor is not supported yet',
          classDeclarations[i].offset,
          hint: 'give the superclass only field initializers, '
              'or set things up in a method the subclass calls',
        );
      }
    }
  }

  /// Superclass fields come first, then the subclass's own.
  List<String> effectiveFields(int index) {
    final superIdx = classSuper[index];
    final inherited = superIdx == null ? <String>[] : effectiveFields(superIdx);
    final own = classFields[index];
    for (final f in own) {
      if (inherited.contains(f)) {
        throw CompileError(
          "'$f' is already a field of the superclass",
          classDeclarations[index].offset,
        );
      }
    }
    return [...inherited, ...own];
  }

  List<(String, Expression)> effectiveFieldInits(int index) {
    final superIdx = classSuper[index];
    final inherited = superIdx == null
        ? <(String, Expression)>[]
        : effectiveFieldInits(superIdx);
    return [...inherited, ...classFieldInits[index]];
  }

  /// Turns `package:moth/widgets.dart` into a path on disk.
  ///
  /// Two ways, in order. A `.dart_tool/package_config.json` beside the program
  /// — what `dart pub get` writes — is authoritative, so a program in a real
  /// pub package resolves the way the Dart SDK would. Failing that, moth's own
  /// packages ship next to the compiler, so `package:moth/...` works in a
  /// bare directory with no pub setup at all. That second path is what keeps
  /// the first program someone writes a single file.
  String _resolvePackageUri(String uri, int offset) {
    final rest = uri.substring('package:'.length);
    final slash = rest.indexOf('/');
    if (slash <= 0 || slash == rest.length - 1) {
      throw CompileError("'$uri' is missing a file after the package name",
          offset,
          hint: 'it should look like package:moth/moth.dart');
    }
    final packageName = rest.substring(0, slash);
    final filePath = rest.substring(slash + 1);

    for (final root in _packageRoots(packageName)) {
      final candidate = p.join(root, filePath);
      if (File(candidate).existsSync()) return candidate;
    }
    throw CompileError("cannot find '$uri'", offset,
        hint: packageName == 'moth'
            ? "moth's libraries should sit in packages/moth/lib beside the "
                'compiler — is the checkout complete?'
            : "add it to pubspec.yaml and run 'dart pub get'");
  }

  /// Candidate lib/ directories for a package, best source first.
  List<String> _packageRoots(String name) {
    final roots = <String>[];

    // What `dart pub get` wrote, if the program lives in a pub package.
    var dir = Directory(p.dirname(p.absolute(path)));
    while (true) {
      final cfg = File(p.join(dir.path, '.dart_tool', 'package_config.json'));
      if (cfg.existsSync()) {
        try {
          final json = jsonDecode(cfg.readAsStringSync());
          for (final pkg in (json['packages'] as List)) {
            if (pkg['name'] != name) continue;
            final rootUri = pkg['rootUri'] as String;
            final packageUri = (pkg['packageUri'] as String?) ?? 'lib/';
            final base = rootUri.startsWith('file://')
                ? Uri.parse(rootUri).toFilePath()
                : p.normalize(p.join(cfg.parent.path, rootUri));
            roots.add(p.normalize(p.join(base, packageUri)));
          }
        } on FormatException {
          // A broken package_config is not worth failing the build over; the
          // bundled copy below is very likely what was wanted anyway.
        }
        break;
      }
      final parent = dir.parent;
      if (parent.path == dir.path) break;
      dir = parent;
    }

    // moth's own packages, which ship with the compiler.
    final here = p.dirname(p.dirname(p.absolute(Platform.script.toFilePath())));
    for (final guess in [
      p.join(here, '..', '..', 'packages', name, 'lib'),
      p.join(here, '..', 'packages', name, 'lib'),
    ]) {
      roots.add(p.normalize(guess));
    }
    return roots;
  }

  /// Distinguishes a setter from a getter or method of the same name. Dart
  /// lets a class declare both `int get x` and `set x(int v)`, so the name
  /// alone is not a unique key; the VM tells them apart by arity instead, and
  /// the trailing '=' is stripped before the name reaches the class table.
  static String _memberKey(MethodDeclaration m) =>
      m.isSetter ? '${m.name.lexeme}=' : m.name.lexeme;

  static String _plainName(String key) =>
      key.endsWith('=') ? key.substring(0, key.length - 1) : key;

  /// Inherited methods, with the subclass's own replacing them by name.
  Map<String, int> effectiveMethodSlots(int index) {
    final superIdx = classSuper[index];
    final merged =
        superIdx == null ? <String, int>{} : effectiveMethodSlots(superIdx);
    for (final (name, slot) in classMethodSlots[index]) {
      merged[name] = slot;
    }
    return merged;
  }

  /// Constructors and methods are appended after the top-level functions, so
  /// their indices are predictable before any body is lowered.
  void _reserveMemberIndices() {
    var next = _declarations.length;
    for (var i = 0; i < classDeclarations.length; i++) {
      final decl = classDeclarations[i];
      // A class with field initializers needs a constructor even if it
      // declares none, so those initializers have somewhere to run. The
      // inherited ones count: a subclass with no fields of its own still has
      // to run its superclass's, which is why this asks for the effective
      // list rather than the class's own.
      final hasCtor = decl.members.any((m) => m is ConstructorDeclaration);
      if (hasCtor || effectiveFieldInits(i).isNotEmpty) {
        classCtorIndex[i] = next++;
      }
      final slots = <(String, int)>[];
      for (final member in decl.members) {
        if (member is MethodDeclaration) {
          slots.add((_memberKey(member), next++));
        }
      }
      classMethodSlots.add(slots);
    }
    _reservedCount = next;
  }

  ClassBlob _compileClass(int index, List<FunctionBlob?> functions) {
    final decl = classDeclarations[index];
    // Methods see the inherited members too, so a subclass method can reach
    // a superclass field or call a superclass method without qualification.
    final fields = effectiveFields(index);
    final inheritedMethods = effectiveMethodSlots(index);
    final methodNames = [
      for (final k in inheritedMethods.keys)
        if (!k.endsWith('=')) k,
    ];

    var ctor = noCtor;
    final fieldInits = effectiveFieldInits(index);
    final declaredCtor =
        decl.members.whereType<ConstructorDeclaration>().firstOrNull;

    // The constructor is emitted first so its index is independent of where
    // it appears among the members (and exists even when synthesized).
    if (classCtorIndex.containsKey(index)) {
      ctor = classCtorIndex[index]!;
      functions[ctor] = (FunctionCompiler.member(this, fields, methodNames,
              isConstructor: true)
          .compileMember(
        name: '${decl.name.lexeme}()',
        params: declaredCtor?.parameters.parameters ?? <FormalParameter>[],
        body: declaredCtor?.body,
        offset: declaredCtor?.offset ?? decl.offset,
        fieldInits: fieldInits,
      ));
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
        final reserved = classMethodSlots[index]
            .firstWhere((s) => s.$1 == _memberKey(member))
            .$2;
        functions[reserved] = FunctionCompiler.member(this, fields, methodNames,
                isConstructor: false)
            .compileMember(
          name: '${decl.name.lexeme}.${member.name.lexeme}',
          params: member.parameters?.parameters ?? const [],
          body: member.body,
          offset: member.offset,
          isSetter: member.isSetter,
        );
        continue;
      }

      throw CompileError(
          'this class member is not supported yet', member.offset);
    }

    return ClassBlob(
      constants.addString(decl.name.lexeme),
      [for (final f in fields) constants.addString(f)],
      // Inherited entries included, own ones already replacing them by name.
      [
        for (final entry in inheritedMethods.entries)
          (constants.addString(_plainName(entry.key)), entry.value),
      ],
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
    if (decl.withClause != null || decl.implementsClause != null) {
      throw CompileError(
        'mixins and implements are not supported yet',
        decl.offset,
        hint: 'single inheritance with "extends" works',
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
        if (!seen.add(_memberKey(member))) {
          throw CompileError(
            "'${member.name.lexeme}' is declared twice in this class",
            member.offset,
          );
        }
      }
    }

    classIndex[name] = classDeclarations.length;
    _classUnit[classDeclarations.length] = currentUnit!;
    classDeclarations.add(decl);
    classFields.add(fields);
    classFieldInits.add(inits);
    classSuper.add(null);
    _pendingSuperNames.add(decl.extendsClause?.superclass.name2.lexeme);
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
