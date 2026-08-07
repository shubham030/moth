import 'package:analyzer/dart/ast/ast.dart';

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

  FunctionCompiler(this.unit, this.decl) : globalInits = null;

  FunctionCompiler.globalsInit(this.unit, this.globalInits) : decl = null;

  FunctionBlob compile() {
    if (globalInits != null) return _compileGlobalsInit();
    final decl = this.decl!;
    final params = decl.functionExpression.parameters?.parameters ?? const [];
    for (final p in params) {
      if (p is! SimpleFormalParameter) {
        throw CompileError(
          'only plain positional parameters are supported yet',
          p.offset,
          hint: 'optional and named parameters arrive in a later milestone',
        );
      }
      _declare(p.name!.lexeme, p.offset);
    }
    final arity = params.length;

    final body = decl.functionExpression.body;
    if (body is BlockFunctionBody) {
      _block(body.block);
    } else if (body is ExpressionFunctionBody) {
      _expression(body.expression);
      _emit(Op.ret);
    } else {
      throw CompileError('this function has no body', decl.offset);
    }
    _emit(Op.retNull);

    return FunctionBlob(
      unit.constants.addString(decl.name.lexeme),
      arity,
      _maxSlots,
      code,
    );
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
    if (parts is! ForPartsWithDeclarations &&
        parts is! ForPartsWithExpression) {
      throw CompileError(
        'only counting for-loops are supported yet',
        stmt.offset,
        hint: 'try "for (var i = 0; i < n; i++)" — for-in arrives with lists',
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
      case MethodInvocation():
        _call(expr);
      default:
        throw CompileError(
          'this kind of expression is not supported yet',
          expr.offset,
          hint: 'M1a supports numbers, bools, variables, arithmetic and calls',
        );
    }
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
    if (slot == null) {
      throw CompileError("'${target.name}' is not defined", target.offset);
    }
    return slot;
  }

  void _assignment(AssignmentExpression expr) {
    final slot = _assignableSlot(expr.leftHandSide);
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

  void _postfix(PostfixExpression expr) {
    final op = expr.operator.lexeme;
    if (op != '++' && op != '--') {
      throw CompileError("'$op' is not supported yet", expr.operator.offset);
    }
    final slot = _assignableSlot(expr.operand);
    _emitLoadSlot(slot);
    _emit(Op.dup); // the old value stays as this expression's result
    _emit(Op.int8);
    _emit(1);
    _emit(op == '++' ? Op.add : Op.sub);
    _emitStoreSlot(slot);
  }

  void _call(MethodInvocation call) {
    if (call.target != null) {
      throw CompileError(
        'method calls on objects are not supported yet',
        call.offset,
        hint: 'M1a has top-level functions only',
      );
    }
    final name = call.methodName.name;
    final args = call.argumentList.arguments;
    for (final a in args) {
      if (a is NamedExpression) {
        throw CompileError('named arguments are not supported yet', a.offset);
      }
    }

    final nativeArgc = kNatives[name];
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
    final target = unit.functionArity(index);
    _checkArgc(name, args.length, target, call.offset);
    for (final a in args) {
      _expression(a);
    }
    _emit(Op.call);
    _emitU16(index);
    _emit(target);
  }

  void _checkArgc(String name, int got, int want, int offset) {
    if (got == want) return;
    throw CompileError(
      "'$name' takes $want argument${want == 1 ? '' : 's'}, but got $got",
      offset,
    );
  }
}
