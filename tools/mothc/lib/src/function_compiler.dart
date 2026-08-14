import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/dart/ast/token.dart';

import 'compiler.dart';
import 'errors.dart';
import 'opcodes.dart';
import 'writer.dart';

class _Loop {
  final List<int> breaks = [];
  final List<int> continues = [];
}

/// Lowers one function body to bytecode.
/// Where a name lives: a frame slot, or a top-level variable slot.
typedef Slot = ({bool isGlobal, int index});

class FunctionCompiler {
  final Compiler unit;
  final FunctionDeclaration? decl;

  /// When set, this compiles the synthetic `<globals>` initializer instead of
  /// a declared function body.
  final List<(int, Expression)>? globalInits;

  final List<int> code = [];
  final List<Map<String, int>> _scopes = [{}];
  final List<_Loop> _loops = [];
  int _nextSlot = 0;
  int _maxSlots = 0;

  /// Fields of the enclosing class, so a bare name inside a method can mean
  /// `this.name`. Empty for top-level functions.
  final List<String> enclosingFields;

  /// Method names of the enclosing class, so a bare call inside a method can
  /// mean `this.name(...)`.
  final List<String> _enclosingMethods;

  /// Setters declared on the enclosing class. Kept apart from the methods
  /// because they are assignable but not callable — folding them together
  /// would let a bare `brightness()` compile into a setter call.
  final List<String> _enclosingSetters;

  /// A constructor returns `this` and may have initializing formals.
  final bool isConstructor;

  /// True when slot 0 holds a receiver — methods, constructors and lambdas.
  /// A lambda written inside one captures that receiver.
  final bool isMember;

  /// Locals of the enclosing function, when this is a lambda. Naming one is
  /// an error rather than a silent copy: moth cannot capture locals yet.
  final Set<String> outerLocals = {};

  FunctionCompiler(this.unit, this.decl)
      : globalInits = null,
        enclosingFields = const [],
        _enclosingMethods = const [],
        _enclosingSetters = const [],
        isConstructor = false,
        isMember = false;

  FunctionCompiler.globalsInit(this.unit, this.globalInits)
      : decl = null,
        enclosingFields = const [],
        _enclosingMethods = const [],
        _enclosingSetters = const [],
        isConstructor = false,
        isMember = false;

  FunctionCompiler.member(
    this.unit,
    this.enclosingFields,
    this._enclosingMethods,
    this._enclosingSetters, {
    required this.isConstructor,
  })  : decl = null,
        globalInits = null,
        isMember = true;

  FunctionBlob compile() {
    if (globalInits != null) return _compileGlobalsInit();
    final decl = this.decl!;
    final params =
        decl.functionExpression.parameters?.parameters ?? <FormalParameter>[];
    final arity = _declareParams(params);

    final body = decl.functionExpression.body;
    _compileBody(body, decl.offset);

    return FunctionBlob(
      unit.constants.addString(decl.name.lexeme),
      arity,
      _maxSlots,
      code,
    );
  }

  /// Methods and constructors: slot 0 is the receiver, so `this` is free.
  FunctionBlob compileMember({
    required String name,
    required List<FormalParameter> params,
    required FunctionBody? body,
    required int offset,
    List<(String, Expression)> fieldInits = const [],
    bool isSetter = false,
  }) {
    _declare('this', offset);
    final arity = 1 + _declareParams(params);

    // Field initializers run first, exactly as Dart orders them.
    for (final (field, init) in fieldInits) {
      _emitThis();
      _expression(init);
      _emit(Op.setProp);
      _emitU16(unit.constants.addString(field));
      _emit(Op.pop);
    }

    // `Pin(this.number)` assigns straight into the field before the body runs.
    for (final p in params) {
      final inner = p is DefaultFormalParameter ? p.parameter : p;
      if (inner is FieldFormalParameter) {
        final fieldName = inner.name.lexeme;
        _emit(Op.load);
        _emit(0); // this
        _emit(Op.load);
        _emit(_lookup(fieldName)!);
        _emit(Op.setProp);
        _emitU16(unit.constants.addString(fieldName));
        _emit(Op.pop);
      }
    }

    if (body != null) _compileBodyStatements(body, offset);

    if (isConstructor) {
      _emit(Op.load);
      _emit(0);
      _emit(Op.ret); // constructors evaluate to the new instance
    } else if (isSetter) {
      // `a.b = v` evaluates to v in Dart. OP_SET_PROP leaves whatever the
      // setter returned, so returning the parameter keeps that true and keeps
      // the opcode's stack effect the same as a plain field write.
      _emit(Op.load);
      _emit(1); // slot 0 is the receiver, slot 1 the assigned value
      _emit(Op.ret);
    } else {
      _emit(Op.retNull);
    }

    return FunctionBlob(unit.constants.addString(name), arity, _maxSlots, code);
  }

  int _declareParams(List<FormalParameter> params) {
    for (final p in params) {
      final inner = p is DefaultFormalParameter ? p.parameter : p;
      if (inner is SimpleFormalParameter) {
        _declare(inner.name!.lexeme, inner.offset);
      } else if (inner is FieldFormalParameter) {
        _declare(inner.name.lexeme, inner.offset);
      } else {
        throw CompileError(
          'only plain positional parameters are supported yet',
          p.offset,
          hint: 'optional and named parameters arrive in a later milestone',
        );
      }
    }
    return params.length;
  }

  void _compileBody(FunctionBody body, int offset) {
    _compileBodyStatements(body, offset);
    _emit(Op.retNull);
  }

  void _compileBodyStatements(FunctionBody body, int offset) {
    // `await` is rejected where it appears, but a body marked async without
    // one used to compile as though the keyword were not there — so a function
    // returning Future<int> in Dart returned a plain int here, and a program
    // that printed it silently disagreed with Dart. Refusing the declaration
    // is the only honest answer until there is an event loop to run it on.
    if (body.isAsynchronous) {
      throw CompileError(
        'async functions are not supported yet',
        body.offset,
        hint: 'moth has no event loop, so there is nothing for a Future to '
            'complete on — write it synchronously, and use millis() to spread '
            'slow work across frames',
      );
    }
    if (body.isGenerator) {
      throw CompileError(
        'generator functions (sync* and async*) are not supported yet',
        body.offset,
        hint: 'return a List instead',
      );
    }

    if (body is BlockFunctionBody) {
      _block(body.block);
    } else if (body is ExpressionFunctionBody) {
      _expression(body.expression);
      _emit(Op.ret);
    } else if (body is EmptyFunctionBody) {
      // a constructor with no body, e.g. Pin(this.number);
    } else {
      throw CompileError('this function has no body', offset);
    }
  }

  FunctionBlob _compileGlobalsInit() {
    for (final (slot, expr) in globalInits!) {
      _expression(expr);
      _emit(Op.storeGlobal);
      _emitU16(slot);
    }
    _emit(Op.retNull);
    return FunctionBlob(
        unit.constants.addString('<globals>'), 0, _maxSlots, code);
  }

  // ---- emit helpers -----------------------------------------------------

  void _emit(int byte) => code.add(byte & 0xFF);
  void _emitU16(int v) {
    code.add(v & 0xFF);
    code.add((v >> 8) & 0xFF);
  }

  /// Emits a jump with a placeholder operand; returns the patch site.
  int _emitJump(int op) {
    _emit(op);
    final site = code.length;
    _emitU16(0);
    return site;
  }

  void _patch(int site) {
    final offset = code.length - (site + 2);
    if (offset < -32768 || offset > 32767) {
      throw CompileError(
          'this function is too large to compile', decl?.offset ?? 0);
    }
    code[site] = offset & 0xFF;
    code[site + 1] = (offset >> 8) & 0xFF;
  }

  void _patchTo(int site, int target) {
    final offset = target - (site + 2);
    if (offset < -32768 || offset > 32767) {
      throw CompileError(
          'this function is too large to compile', decl?.offset ?? 0);
    }
    code[site] = offset & 0xFF;
    code[site + 1] = (offset >> 8) & 0xFF;
  }

  void _emitLoop(int target) {
    _emit(Op.jump);
    final site = code.length;
    _emitU16(0);
    _patchTo(site, target);
  }

  // ---- scopes -----------------------------------------------------------

  int _declare(String name, int offset) {
    if (_scopes.last.containsKey(name)) {
      throw CompileError("'$name' is already declared in this scope", offset);
    }
    if (_nextSlot >= 255) {
      throw CompileError('too many variables in this function', offset);
    }
    final slot = _nextSlot++;
    if (_nextSlot > _maxSlots) _maxSlots = _nextSlot;
    _scopes.last[name] = slot;
    return slot;
  }

  int? _lookup(String name) {
    for (var i = _scopes.length - 1; i >= 0; i--) {
      final slot = _scopes[i][name];
      if (slot != null) return slot;
    }
    return null;
  }

  void _pushScope() => _scopes.add({});
  void _popScope() {
    final scope = _scopes.removeLast();
    _nextSlot -= scope.length; // slots are reusable once out of scope
  }

  // ---- statements -------------------------------------------------------

  void _block(Block block) {
    _pushScope();
    for (final s in block.statements) {
      _statement(s);
    }
    _popScope();
  }

  void _statement(Statement stmt) {
    switch (stmt) {
      case Block():
        _block(stmt);
      case VariableDeclarationStatement():
        _variableDeclaration(stmt);
      case ExpressionStatement():
        _expression(stmt.expression);
        _emit(Op.pop); // every expression leaves exactly one value
      case IfStatement():
        _ifStatement(stmt);
      case WhileStatement():
        _whileStatement(stmt);
      case ForStatement():
        _forStatement(stmt);
      case ReturnStatement():
        if (stmt.expression == null) {
          _emit(Op.retNull);
        } else {
          _expression(stmt.expression!);
          _emit(Op.ret);
        }
      case BreakStatement():
        if (_loops.isEmpty) {
          throw CompileError(
            "'break' is only allowed inside a loop",
            stmt.offset,
          );
        }
        _loops.last.breaks.add(_emitJump(Op.jump));
      case ContinueStatement():
        if (_loops.isEmpty) {
          throw CompileError(
            "'continue' is only allowed inside a loop",
            stmt.offset,
          );
        }
        _loops.last.continues.add(_emitJump(Op.jump));
      case EmptyStatement():
        break;
      default:
        throw CompileError(
          'this kind of statement is not supported yet',
          stmt.offset,
          hint: 'M1a supports var, if, while, for, return, break and continue',
        );
    }
  }

  void _variableDeclaration(VariableDeclarationStatement stmt) {
    for (final v in stmt.variables.variables) {
      if (v.initializer == null) {
        _emit(Op.pushNull);
      } else {
        _expression(v.initializer!);
      }
      final slot = _declare(v.name.lexeme, v.offset);
      _emit(Op.store);
      _emit(slot);
    }
  }

  void _ifStatement(IfStatement stmt) {
    _expression(stmt.expression);
    final elseJump = _emitJump(Op.jumpIfFalse);
    _statement(stmt.thenStatement);
    if (stmt.elseStatement != null) {
      final endJump = _emitJump(Op.jump);
      _patch(elseJump);
      _statement(stmt.elseStatement!);
      _patch(endJump);
    } else {
      _patch(elseJump);
    }
  }

  void _whileStatement(WhileStatement stmt) {
    final start = code.length;
    _expression(stmt.condition);
    final exit = _emitJump(Op.jumpIfFalse);

    final loop = _Loop();
    _loops.add(loop);
    _statement(stmt.body);
    _loops.removeLast();

    for (final site in loop.continues) {
      _patchTo(site, start);
    }
    _emitLoop(start);
    _patch(exit);
    for (final site in loop.breaks) {
      _patch(site);
    }
  }

  void _forStatement(ForStatement stmt) {
    final parts = stmt.forLoopParts;
    if (parts is ForEachPartsWithDeclaration) {
      _forInStatement(stmt, parts);
      return;
    }
    if (parts is! ForPartsWithDeclarations &&
        parts is! ForPartsWithExpression) {
      throw CompileError(
        'this kind of for-loop is not supported yet',
        stmt.offset,
        hint: 'try "for (var i = 0; i < n; i++)" or "for (final x in list)"',
      );
    }

    _pushScope();
    Expression? condition;
    List<Expression> updaters;

    if (parts is ForPartsWithDeclarations) {
      for (final v in parts.variables.variables) {
        if (v.initializer == null) {
          _emit(Op.pushNull);
        } else {
          _expression(v.initializer!);
        }
        final slot = _declare(v.name.lexeme, v.offset);
        _emit(Op.store);
        _emit(slot);
      }
      condition = parts.condition;
      updaters = parts.updaters;
    } else {
      parts as ForPartsWithExpression;
      if (parts.initialization != null) {
        _expression(parts.initialization!);
        _emit(Op.pop);
      }
      condition = parts.condition;
      updaters = parts.updaters;
    }

    final start = code.length;
    int? exit;
    if (condition != null) {
      _expression(condition);
      exit = _emitJump(Op.jumpIfFalse);
    }

    final loop = _Loop();
    _loops.add(loop);
    _statement(stmt.body);
    _loops.removeLast();

    final updateLabel = code.length;
    for (final u in updaters) {
      _expression(u);
      _emit(Op.pop);
    }
    _emitLoop(start);

    if (exit != null) _patch(exit);
    for (final site in loop.breaks) {
      _patch(site);
    }
    for (final site in loop.continues) {
      _patchTo(site, updateLabel);
    }
    _popScope();
  }

  /// `for (final x in list)` lowers to an index walk over hidden locals, so
  /// it needs no iterator protocol.
  void _forInStatement(ForStatement stmt, ForEachPartsWithDeclaration parts) {
    _pushScope();

    // hidden: the iterable, evaluated once, and the cursor
    _expression(parts.iterable);
    final listSlot = _declare(' iterable', stmt.offset);
    _emit(Op.store);
    _emit(listSlot);

    _emit(Op.int8);
    _emit(0);
    final indexSlot = _declare(' index', stmt.offset);
    _emit(Op.store);
    _emit(indexSlot);

    final start = code.length;
    _emit(Op.load);
    _emit(indexSlot);
    _emit(Op.load);
    _emit(listSlot);
    _emit(Op.getProp);
    _emitU16(unit.constants.addString('length'));
    _emit(Op.lt);
    final exit = _emitJump(Op.jumpIfFalse);

    _pushScope();
    _emit(Op.load);
    _emit(listSlot);
    _emit(Op.load);
    _emit(indexSlot);
    _emit(Op.indexGet);
    final itemSlot = _declare(parts.loopVariable.name.lexeme, parts.offset);
    _emit(Op.store);
    _emit(itemSlot);

    final loop = _Loop();
    _loops.add(loop);
    _statement(stmt.body);
    _loops.removeLast();
    _popScope();

    final continueLabel = code.length;
    _emit(Op.load);
    _emit(indexSlot);
    _emit(Op.int8);
    _emit(1);
    _emit(Op.add);
    _emit(Op.store);
    _emit(indexSlot);
    _emitLoop(start);

    _patch(exit);
    for (final site in loop.breaks) {
      _patch(site);
    }
    for (final site in loop.continues) {
      _patchTo(site, continueLabel);
    }
    _popScope();
  }

  // ---- expressions ------------------------------------------------------

  void _expression(Expression expr) {
    switch (expr) {
      case IntegerLiteral():
        _pushInt(expr.value ?? 0, expr.offset);
      case DoubleLiteral():
        _emit(Op.konst);
        _emitU16(unit.constants.addDouble(expr.value));
      case BooleanLiteral():
        _emit(expr.value ? Op.pushTrue : Op.pushFalse);
      case NullLiteral():
        _emit(Op.pushNull);
      case SimpleStringLiteral():
        _emit(Op.konst);
        _emitU16(unit.constants.addString(expr.value));
      case StringInterpolation():
        _interpolation(expr);
      case AdjacentStrings():
        // 'a' 'b' is one string in Dart
        for (var i = 0; i < expr.strings.length; i++) {
          _expression(expr.strings[i]);
          if (i > 0) _emit(Op.add);
        }
      case ParenthesizedExpression():
        _expression(expr.expression);
      case SimpleIdentifier():
        _identifier(expr);
      case AssignmentExpression():
        _assignment(expr);
      case BinaryExpression():
        _binary(expr);
      case PrefixExpression():
        _prefix(expr);
      case PostfixExpression():
        _postfix(expr);
      case ListLiteral():
        _listLiteral(expr);
      case IndexExpression():
        _rejectNullAware(expr.question, expr.offset);
        _expression(expr.target!);
        _expression(expr.index);
        _emit(Op.indexGet);
      case PropertyAccess():
        _rejectNullAware(expr.operator, expr.offset);
        _property(expr.propertyName, expr.target!, expr.offset);
      case PrefixedIdentifier():
        // `list.length` parses as a prefixed identifier when the target is a
        // plain name rather than an expression.
        _property(expr.identifier, expr.prefix, expr.offset);
      case ThisExpression():
        _emitThis();
      case FunctionExpression():
        _lambda(expr);
      case FunctionExpressionInvocation():
        // `handlers[0]()` — the callee is an expression, not a name
        _expression(expr.function);
        for (final a in expr.argumentList.arguments) {
          _expression(a);
        }
        _emit(Op.callValue);
        _emit(expr.argumentList.arguments.length);
      case ConditionalExpression():
        _conditional(expr);
      case MethodInvocation():
        _call(expr);
      case CascadeExpression():
        _cascade(expr);
      default:
        throw CompileError(
          'this kind of expression is not supported yet',
          expr.offset,
          hint: 'M1a supports numbers, bools, variables, arithmetic and calls',
        );
    }
  }

  void _listLiteral(ListLiteral expr) {
    var count = 0;
    for (final element in expr.elements) {
      if (element is! Expression) {
        throw CompileError(
          'spreads and if/for inside list literals are not supported yet',
          element.offset,
        );
      }
      _expression(element);
      count++;
    }
    if (count > 0xFFFF) {
      throw CompileError('list literal is too large', expr.offset);
    }
    _emit(Op.newList);
    _emitU16(count);
  }

  /// `a?.b` and `a?[i]` must skip the access when `a` is null. moth would
  /// instead trap, so they are rejected rather than compiled to the wrong
  /// thing. Covers `?.` operators and the `?` of a null-aware index.
  void _rejectNullAware(Token? token, int offset) {
    if (token == null) return;
    final lexeme = token.lexeme;
    if (lexeme != '?.' && lexeme != '?') return;
    throw CompileError(
      'null-aware access ($lexeme) is not supported yet',
      offset,
      hint: lexeme == '?.'
          ? 'guard explicitly: if (x != null) x.method();'
          : 'guard explicitly: if (x != null) x[i];',
    );
  }

  /// Property reads are resolved by name at run time — the compiler has no
  /// type information, so `x.foo` cannot be turned into a slot here.
  void _property(SimpleIdentifier name, Expression target, int offset) {
    _expression(target);
    _emit(Op.getProp);
    _emitU16(unit.constants.addString(name.name));
  }

  /// [emitTarget] is a callback because the receiver may be an expression
  /// (`sensor.pin = 4`) or an implicit `this` (`pin = 4` inside a method).
  void _propertyAssignment(
    AssignmentExpression expr,
    SimpleIdentifier name,
    void Function() emitTarget,
  ) {
    final op = expr.operator.lexeme;
    emitTarget();
    if (op == '=') {
      _expression(expr.rightHandSide);
    } else {
      final arith = _compoundOps[op];
      if (arith == null) {
        throw CompileError("'$op' is not supported yet", expr.operator.offset);
      }
      emitTarget();
      _emit(Op.getProp);
      _emitU16(unit.constants.addString(name.name));
      _expression(expr.rightHandSide);
      _emit(arith);
    }
    _emit(Op.setProp);
    _emitU16(unit.constants.addString(name.name));
  }

  /// A bare name inside a method that matches a field means `this.name`.
  bool _isField(String name) => enclosingFields.contains(name);

  void _emitThis() {
    _emit(Op.load);
    _emit(0);
  }

  /// `'temp $t C'` lowers to a left-to-right chain of concatenations, with
  /// each embedded value converted first.
  void _interpolation(StringInterpolation expr) {
    var first = true;
    for (final element in expr.elements) {
      if (element is InterpolationString) {
        if (element.value.isEmpty) continue; // the empty edges of '$x'
        _emit(Op.konst);
        _emitU16(unit.constants.addString(element.value));
      } else if (element is InterpolationExpression) {
        _expression(element.expression);
        _emit(Op.toStringOp);
      }
      if (!first) _emit(Op.add);
      first = false;
    }
    if (first) {
      // the whole literal was empty
      _emit(Op.konst);
      _emitU16(unit.constants.addString(''));
    }
  }

  void _pushInt(int value, int offset) {
    if (value >= -128 && value <= 127) {
      _emit(Op.int8);
      _emit(value);
    } else {
      _emit(Op.konst);
      _emitU16(unit.constants.addInt(value));
    }
  }

  /// Locals shadow top-level variables, as in Dart.
  Slot? _resolve(String name) {
    final local = _lookup(name);
    if (local != null) return (isGlobal: false, index: local);
    final global = unit.globalIndex[name];
    if (global != null) return (isGlobal: true, index: global);
    return null;
  }

  void _emitLoadSlot(Slot slot) {
    if (slot.isGlobal) {
      _emit(Op.loadGlobal);
      _emitU16(slot.index);
    } else {
      _emit(Op.load);
      _emit(slot.index);
    }
  }

  void _emitStoreSlot(Slot slot) {
    if (slot.isGlobal) {
      _emit(Op.storeGlobal);
      _emitU16(slot.index);
    } else {
      _emit(Op.store);
      _emit(slot.index);
    }
  }

  void _identifier(SimpleIdentifier id) {
    final slot = _resolve(id.name);
    // A bare name inside a method can be a field or a getter on this object.
    // Both read the same way — OP_GET_PROP tries the field first and falls
    // back to the getter — so one path covers them.
    if (slot == null &&
        (_isField(id.name) || _enclosingMethods.contains(id.name))) {
      _emitThis();
      _emit(Op.getProp);
      _emitU16(unit.constants.addString(id.name));
      return;
    }
    if (slot == null && outerLocals.contains(id.name)) {
      throw CompileError(
        "a closure cannot use '${id.name}' from the enclosing function yet",
        id.offset,
        hint: 'capture is not implemented — make it a top-level variable '
            'or a field, which closures can reach',
      );
    }
    if (slot == null) {
      throw CompileError(
        "'${id.name}' is not defined",
        id.offset,
        hint: 'declare it with "var ${id.name} = ...;" — '
            'at the top of the file, or inside the function that uses it',
      );
    }
    _emitLoadSlot(slot);
  }

  Slot _assignableSlot(Expression target) {
    if (target is! SimpleIdentifier) {
      throw CompileError('only variables can be assigned to', target.offset);
    }
    final slot = _resolve(target.name);
    if (slot == null && outerLocals.contains(target.name)) {
      throw CompileError(
        "a closure cannot use '${target.name}' from the enclosing function yet",
        target.offset,
        hint: 'capture is not implemented — make it a top-level variable '
            'or a field, which closures can reach',
      );
    }
    if (slot == null) {
      throw CompileError("'${target.name}' is not defined", target.offset);
    }
    return slot;
  }

  void _assignment(AssignmentExpression expr) {
    final target = expr.leftHandSide;
    if (target is IndexExpression) {
      _indexAssignment(expr, target);
      return;
    }
    if (target is PropertyAccess) {
      _propertyAssignment(
          expr, target.propertyName, () => _expression(target.target!));
      return;
    }
    if (target is PrefixedIdentifier) {
      _propertyAssignment(
          expr, target.identifier, () => _expression(target.prefix));
      return;
    }
    // A bare name on the left can be a field or a setter on this object. Both
    // write the same way — OP_SET_PROP tries the field first and falls back to
    // the setter — so one path covers them.
    if (target is SimpleIdentifier &&
        _resolve(target.name) == null &&
        (_isField(target.name) ||
            _enclosingMethods.contains(target.name) ||
            _enclosingSetters.contains(target.name))) {
      _propertyAssignment(expr, target, _emitThis);
      return;
    }
    final slot = _assignableSlot(target);
    final op = expr.operator.lexeme;

    if (op == '=') {
      _expression(expr.rightHandSide);
    } else {
      final arith = const {
        '+=': Op.add,
        '-=': Op.sub,
        '*=': Op.mul,
        '/=': Op.div,
        '~/=': Op.idiv,
        '%=': Op.mod,
        '&=': Op.band,
        '|=': Op.bor,
        '^=': Op.bxor,
        '<<=': Op.shl,
        '>>=': Op.shr,
      }[op];
      if (arith == null) {
        throw CompileError("'$op' is not supported yet", expr.operator.offset);
      }
      _emitLoadSlot(slot);
      _expression(expr.rightHandSide);
      _emit(arith);
    }
    // assignment is an expression: leave the value, then store a copy
    _emit(Op.dup);
    _emitStoreSlot(slot);
  }

  /// `list[i] = v` and `list[i] += v`. The compound form evaluates the target
  /// and index twice; both are simple expressions in practice.
  void _indexAssignment(AssignmentExpression expr, IndexExpression target) {
    _rejectNullAware(target.question, target.offset);
    final op = expr.operator.lexeme;
    _expression(target.target!);
    _expression(target.index);

    if (op == '=') {
      _expression(expr.rightHandSide);
    } else {
      final arith = _compoundOps[op];
      if (arith == null) {
        throw CompileError("'$op' is not supported yet", expr.operator.offset);
      }
      _expression(target.target!);
      _expression(target.index);
      _emit(Op.indexGet);
      _expression(expr.rightHandSide);
      _emit(arith);
    }
    _emit(Op.indexSet);
  }

  static const _compoundOps = {
    '+=': Op.add,
    '-=': Op.sub,
    '*=': Op.mul,
    '/=': Op.div,
    '~/=': Op.idiv,
    '%=': Op.mod,
    '&=': Op.band,
    '|=': Op.bor,
    '^=': Op.bxor,
    '<<=': Op.shl,
    '>>=': Op.shr,
  };

  void _binary(BinaryExpression expr) {
    final op = expr.operator.lexeme;

    if (op == '&&' || op == '||') {
      _expression(expr.leftOperand);
      final jump = _emitJump(
        op == '&&' ? Op.jumpIfFalseKeep : Op.jumpIfTrueKeep,
      );
      _emit(Op.pop);
      _expression(expr.rightOperand);
      _patch(jump);
      return;
    }

    const table = {
      '+': Op.add,
      '-': Op.sub,
      '*': Op.mul,
      '/': Op.div,
      '~/': Op.idiv,
      '%': Op.mod,
      '==': Op.eq,
      '!=': Op.ne,
      '<': Op.lt,
      '<=': Op.le,
      '>': Op.gt,
      '>=': Op.ge,
      '&': Op.band,
      '|': Op.bor,
      '^': Op.bxor,
      '<<': Op.shl,
      '>>': Op.shr,
    };
    final code = table[op];
    if (code == null) {
      throw CompileError(
        "the '$op' operator is not supported yet",
        expr.operator.offset,
      );
    }
    _expression(expr.leftOperand);
    _expression(expr.rightOperand);
    _emit(code);
  }

  void _prefix(PrefixExpression expr) {
    final op = expr.operator.lexeme;
    switch (op) {
      case '!':
        _expression(expr.operand);
        _emit(Op.not);
      case '~':
        _expression(expr.operand);
        _emit(Op.bnot);
      case '-':
        final operand = expr.operand;
        if (operand is IntegerLiteral) {
          _pushInt(-(operand.value ?? 0), operand.offset); // fold -literal
        } else {
          _expression(operand);
          _emit(Op.neg);
        }
      case '++':
      case '--':
        if (_incDecOnProperty(expr.operand, op == '++', prefix: true)) break;
        final slot = _assignableSlot(expr.operand);
        _emitLoadSlot(slot);
        _emit(Op.int8);
        _emit(1);
        _emit(op == '++' ? Op.add : Op.sub);
        _emit(Op.dup);
        _emitStoreSlot(slot);
      default:
        throw CompileError("'$op' is not supported yet", expr.operator.offset);
    }
  }

  /// `() { ... }` becomes an ordinary function whose slot 0 is the receiver,
  /// plus a closure object binding it. Capturing locals is not supported, so
  /// the lambda is compiled with the enclosing locals listed as forbidden.
  void _lambda(FunctionExpression expr) {
    final outer = <String>{};
    for (final scope in _scopes) {
      outer.addAll(scope.keys);
    }
    outer.remove('this');

    final sub = FunctionCompiler.member(
      unit,
      enclosingFields,
      _enclosingMethods,
      _enclosingSetters,
      isConstructor: false,
    );
    sub.outerLocals.addAll(outer);

    final blob = sub.compileMember(
      name: '<closure>',
      params: expr.parameters?.parameters ?? <FormalParameter>[],
      body: expr.body,
      offset: expr.offset,
    );

    _emit(Op.closure);
    _emitU16(unit.addFunction(blob));
    _emit(isMember ? 1 : 0); // only a lambda inside a member has a receiver
  }

  void _conditional(ConditionalExpression expr) {
    _expression(expr.condition);
    final toElse = _emitJump(Op.jumpIfFalse);
    _expression(expr.thenExpression);
    final toEnd = _emitJump(Op.jump);
    _patch(toElse);
    _expression(expr.elseExpression);
    _patch(toEnd);
  }

  void _postfix(PostfixExpression expr) {
    final op = expr.operator.lexeme;
    // `x!` asserts non-null. moth has no null-safety checking, so the value
    // simply passes through — it exists so null-safe Dart source compiles.
    if (op == '!') {
      _expression(expr.operand);
      return;
    }
    if (op != '++' && op != '--') {
      throw CompileError("'$op' is not supported yet", expr.operator.offset);
    }
    if (_incDecOnProperty(expr.operand, op == '++', prefix: false)) return;
    final slot = _assignableSlot(expr.operand);
    _emitLoadSlot(slot);
    _emit(Op.dup); // the old value stays as this expression's result
    _emit(Op.int8);
    _emit(1);
    _emit(op == '++' ? Op.add : Op.sub);
    _emitStoreSlot(slot);
  }

  /// `count++` where count is a field, and `obj.count++`. Returns false when
  /// the operand is an ordinary variable, which the caller then handles.
  ///
  /// The receiver goes into a hidden local so it is evaluated exactly once,
  /// and the old/new value into a second so the result can be ordered without
  /// a stack-shuffling opcode.
  bool _incDecOnProperty(Expression operand, bool increment,
      {required bool prefix}) {
    void Function()? emitTarget;
    String? name;

    if (operand is PropertyAccess) {
      _rejectNullAware(operand.operator, operand.offset);
      emitTarget = () => _expression(operand.target!);
      name = operand.propertyName.name;
    } else if (operand is PrefixedIdentifier) {
      emitTarget = () => _expression(operand.prefix);
      name = operand.identifier.name;
    } else if (operand is SimpleIdentifier &&
        _resolve(operand.name) == null &&
        (_isField(operand.name) ||
            _enclosingMethods.contains(operand.name) ||
            _enclosingSetters.contains(operand.name))) {
      // Accessors as well as fields: GET_PROP and SET_PROP both fall back to
      // them, so `value++` in a method works the same way `value = value + 1`
      // already did.
      emitTarget = _emitThis;
      name = operand.name;
    }
    if (emitTarget == null || name == null) return false;

    final nameConst = unit.constants.addString(name);
    _pushScope();
    final targetSlot = _declare(' target', operand.offset);
    final valueSlot = _declare(' value', operand.offset);

    emitTarget();
    _emit(Op.store);
    _emit(targetSlot);

    _emit(Op.load);
    _emit(targetSlot);
    _emit(Op.getProp);
    _emitU16(nameConst);
    if (prefix) {
      _emit(Op.int8);
      _emit(1);
      _emit(increment ? Op.add : Op.sub);
    }
    _emit(Op.store);
    _emit(valueSlot); // prefix: the new value; postfix: the old one

    _emit(Op.load);
    _emit(targetSlot);
    _emit(Op.load);
    _emit(valueSlot);
    if (!prefix) {
      _emit(Op.int8);
      _emit(1);
      _emit(increment ? Op.add : Op.sub);
    }
    _emit(Op.setProp);
    _emitU16(nameConst);
    _emit(Op.pop);

    _emit(Op.load);
    _emit(valueSlot);
    _popScope();
    return true;
  }

  /// `target..a = 1..b()` — the target is evaluated once and stays on the
  /// stack; each section works on a copy and throws its own result away. The
  /// whole expression is the target, which is what lets a widget be
  /// configured and passed along in one expression.
  void _cascade(CascadeExpression expr) {
    _expression(expr.target);

    for (final section in expr.cascadeSections) {
      _emit(Op.dup);
      _cascadeSection(section);
      _emit(Op.pop); // a section's value is discarded; the target is the result
    }
  }

  /// One `..something`, compiled with the receiver already on the stack.
  void _cascadeSection(Expression section) {
    if (section is AssignmentExpression) {
      final target = section.leftHandSide;
      if (target is! PropertyAccess) {
        throw CompileError(
          'only property assignment works in a cascade so far',
          section.offset,
          hint: 'write "..name = value"',
        );
      }
      if (target.target != null) {
        // `..a.b = v` would assign b on a, not on the cascade target. The
        // compiler cannot reach through that yet, and quietly assigning to
        // the wrong object is worse than refusing.
        throw CompileError(
          'a cascade cannot reach through another property yet',
          section.offset,
          hint: 'assign it separately: "var x = thing.a; x.b = v;"',
        );
      }

      // The receiver is already on the stack. A compound assignment needs it
      // twice — once to read the old value, once to write the new one — so
      // the second ask duplicates what is there rather than re-evaluating.
      var used = false;
      void receiver() {
        if (!used) {
          used = true;
          return;
        }
        _emit(Op.dup);
      }

      _propertyAssignment(section, target.propertyName, receiver);
      return;
    }

    if (section is MethodInvocation) {
      if (section.target != null) {
        throw CompileError(
          'a cascade cannot call through another property yet',
          section.offset,
          hint: 'call it separately: "var x = thing.list; x.add(v);"',
        );
      }
      final args = section.argumentList.arguments;
      for (final a in args) {
        if (a is NamedExpression) {
          throw CompileError('named arguments are not supported yet', a.offset);
        }
        _expression(a);
      }
      _emit(Op.invoke);
      _emitU16(unit.constants.addString(section.methodName.name));
      _emit(args.length);
      return;
    }

    throw CompileError(
      'this kind of cascade section is not supported yet',
      section.offset,
      hint: 'assignments and method calls work',
    );
  }

  /// Emits one value per declared parameter, in slot order.
  ///
  /// Named arguments are matched to parameters here rather than at run time,
  /// which is what makes them free: the VM still sees a plain positional
  /// call. Anything the caller left out contributes its default, or null.
  /// Only works where the callee is known statically — a constructor or a
  /// top-level function — which is exactly where a widget tree needs it.
  void _emitArgsInSlotOrder(String what, List<FormalParameter> params,
      List<Expression> args, int offset) {
    final positional = <Expression>[];
    final named = <String, Expression>{};
    for (final a in args) {
      if (a is NamedExpression) {
        final label = a.name.label.name;
        if (named.containsKey(label)) {
          throw CompileError("'$label' is given twice", a.offset);
        }
        named[label] = a.expression;
      } else {
        if (named.isNotEmpty) {
          throw CompileError(
            'positional arguments must come before named ones',
            a.offset,
          );
        }
        positional.add(a);
      }
    }

    var next = 0;
    for (final p in params) {
      final inner = p is DefaultFormalParameter ? p.parameter : p;
      final name = inner is SimpleFormalParameter
          ? inner.name?.lexeme
          : (inner is FieldFormalParameter ? inner.name.lexeme : null);

      if (p.isNamed) {
        final supplied = name == null ? null : named.remove(name);
        if (supplied != null) {
          _expression(supplied);
        } else if (p is DefaultFormalParameter && p.defaultValue != null) {
          _expression(p.defaultValue!);
        } else {
          _emit(Op.pushNull);
        }
        continue;
      }

      if (next < positional.length) {
        _expression(positional[next++]);
      } else if (p is DefaultFormalParameter && p.defaultValue != null) {
        _expression(p.defaultValue!);
      } else {
        throw CompileError(
          "'$what' is missing a value for '${name ?? "a parameter"}'",
          offset,
        );
      }
    }

    if (named.isNotEmpty) {
      throw CompileError(
        "'$what' has no parameter named '${named.keys.first}'",
        offset,
      );
    }
    if (next < positional.length) {
      throw CompileError(
        "'$what' takes ${params.length} arguments, but got more",
        offset,
      );
    }
  }

  /// True when the callee declares any named or defaulted parameter, or the
  /// caller passes a named argument — the cases slot matching exists for.
  static bool _needsSlotMatching(
          List<FormalParameter> params, List<Expression> args) =>
      params.any((p) => p.isNamed || p is DefaultFormalParameter) ||
      args.any((a) => a is NamedExpression);

  void _call(MethodInvocation call) {
    _rejectNullAware(call.operator, call.offset);
    final name = call.methodName.name;
    final args = call.argumentList.arguments;

    // Image('path.png') is also a compile-time request to embed the file —
    // the board has no filesystem to load it from later. Only a literal can
    // be embedded, and catching a computed path here beats a blank square
    // at run time.
    if (name == 'Image' && call.target == null && args.isNotEmpty) {
      final first = args.first;
      if (first is SimpleStringLiteral) {
        unit.imageRefs.putIfAbsent(first.value, () => first.offset);
      } else if (first is! NamedExpression) {
        throw CompileError(
          "Image needs a literal path (like Image('logo.png')) so the "
          'compiler can embed the file in the program',
          first.offset,
          hint: 'the board has no filesystem — images travel inside the '
              '.mothb blob, which means the compiler must see the path',
        );
      }
    }

    // Method call: the receiver and its class are only known at run time, so
    // there is no declaration to match names against. Constructors and
    // top-level functions are resolved here and handle named arguments below.
    if (call.target != null) {
      for (final a in args) {
        if (a is NamedExpression) {
          throw CompileError(
            'named arguments only work where the callee is known: a '
            'constructor or a top-level function',
            a.offset,
            hint: 'a method is dispatched on the receiver at run time, so its '
                'parameter names are not known here',
          );
        }
      }
      _expression(call.target!);
      for (final a in args) {
        _expression(a);
      }
      _emit(Op.invoke);
      _emitU16(unit.constants.addString(name));
      _emit(args.length);
      return;
    }

    // A bare call inside a method may be another method on this object.
    if (enclosingFields.isNotEmpty || unit.classIndex.isNotEmpty) {
      if (!unit.functionIndex.containsKey(name) &&
          !kNatives.containsKey(name) &&
          !unit.classIndex.containsKey(name) &&
          _enclosingMethods.contains(name)) {
        _emitThis();
        for (final a in args) {
          _expression(a);
        }
        _emit(Op.invoke);
        _emitU16(unit.constants.addString(name));
        _emit(args.length);
        return;
      }
    }

    // `Point(3, 4)` — with no `new` and no type resolution, a constructor
    // call is indistinguishable from a function call until we check the name.
    // A variable holding a function: `handler()`, or a field `onTap()`.
    final asValue = _resolve(name);
    if (asValue != null) {
      _emitLoadSlot(asValue);
      for (final a in args) {
        _expression(a);
      }
      _emit(Op.callValue);
      _emit(args.length);
      return;
    }
    if (_isField(name)) {
      _emitThis();
      _emit(Op.getProp);
      _emitU16(unit.constants.addString(name));
      for (final a in args) {
        _expression(a);
      }
      _emit(Op.callValue);
      _emit(args.length);
      return;
    }

    final classIdx = unit.classIndex[name];
    if (classIdx != null) {
      _emit(Op.newInstance);
      _emitU16(classIdx);
      final ctorIndex = unit.classCtorIndex[classIdx];
      final ctorParams = unit.classCtorParams(classIdx);

      if (ctorIndex == null) {
        if (args.isNotEmpty) {
          _checkArgc(name, args.length, 0, call.offset);
        }
        return; // default constructor: the fresh instance is the result
      }

      if (_needsSlotMatching(ctorParams, args)) {
        _emitArgsInSlotOrder(name, ctorParams, args, call.offset);
      } else {
        // Arity is checked here so a wrong count is a compile error with a
        // location, not a runtime trap reported as "corrupt program".
        _checkArgc(
            name, args.length, unit.classCtorArity(classIdx) ?? 0, call.offset);
        for (final a in args) {
          _expression(a);
        }
      }
      _emit(Op.call);
      _emitU16(ctorIndex);
      _emit(ctorParams.length + 1); // slot 0 is the new instance
      return;
    }

    final nativeArgc = kNatives[name];
    if (nativeArgc != null) {
      for (final a in args) {
        if (a is NamedExpression) {
          throw CompileError(
            "'$name' is a built-in and takes its arguments positionally",
            a.offset,
          );
        }
      }
    }
    if (nativeArgc != null) {
      _checkArgc(name, args.length, nativeArgc, call.offset);
      for (final a in args) {
        _expression(a);
      }
      _emit(Op.native);
      _emitU16(unit.nativeRef(name));
      _emit(nativeArgc);
      return;
    }

    final index = unit.functionIndex[name];
    if (index == null) {
      throw CompileError(
        "'$name' is not defined",
        call.offset,
        hint: 'built-ins available: ${kNatives.keys.join(', ')}',
      );
    }
    final params = unit.functionParams(index);
    if (_needsSlotMatching(params, args)) {
      _emitArgsInSlotOrder(name, params, args, call.offset);
    } else {
      _checkArgc(name, args.length, unit.functionArity(index), call.offset);
      for (final a in args) {
        _expression(a);
      }
    }
    _emit(Op.call);
    _emitU16(index);
    _emit(params.length);
  }

  void _checkArgc(String name, int got, int want, int offset) {
    if (got == want) return;
    throw CompileError(
      "'$name' takes $want argument${want == 1 ? '' : 's'}, but got $got",
      offset,
    );
  }
}
