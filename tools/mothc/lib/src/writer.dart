import 'dart:convert';
import 'dart:typed_data';

const int bytecodeVersion = 3;

/// Sentinel for "this program has no top-level initializers to run".
const int noInit = 0xFFFF;

const int _tagInt = 0;
const int _tagDouble = 1;
const int _tagString = 2;
const int _tagBool = 3;
const int _tagNull = 4;

class _Const {
  final int tag;
  final Object? value;
  const _Const(this.tag, this.value);

  @override
  bool operator ==(Object other) =>
      other is _Const && other.tag == tag && other.value == value;
  @override
  int get hashCode => Object.hash(tag, value);
}

/// Constant pool with deduplication.
class ConstantPool {
  final List<_Const> _items = [];
  final Map<_Const, int> _index = {};

  int _add(_Const c) {
    final existing = _index[c];
    if (existing != null) return existing;
    if (_items.length >= 0xFFFF) {
      throw StateError('too many constants (max 65535)');
    }
    _items.add(c);
    return _index[c] = _items.length - 1;
  }

  int addInt(int v) => _add(_Const(_tagInt, v));
  int addDouble(double v) => _add(_Const(_tagDouble, v));
  int addString(String v) => _add(_Const(_tagString, v));
  int addBool(bool v) => _add(_Const(_tagBool, v));
  int addNull() => _add(const _Const(_tagNull, null));

  int get length => _items.length;
  List<_Const> get items => _items;
}

class FunctionBlob {
  final int nameConst;
  final int arity;
  final int nlocals;
  final List<int> code;
  FunctionBlob(this.nameConst, this.arity, this.nlocals, this.code);
}

class NativeRef {
  final int nameConst;
  final int argc;
  NativeRef(this.nameConst, this.argc);
}

const int noCtor = 0xFFFF;

class ClassBlob {
  final int nameConst;
  final List<int> fieldNameConsts;

  /// name constant -> function index
  final List<(int, int)> methods;
  final int ctor;

  ClassBlob(this.nameConst, this.fieldNameConsts, this.methods, this.ctor);
}

/// Serializes a program to the .mothb format (docs/BYTECODE.md).
Uint8List writeBlob({
  required ConstantPool constants,
  required List<NativeRef> natives,
  required List<FunctionBlob> functions,
  required int entry,
  int globalCount = 0,
  int init = noInit,
  List<ClassBlob> classes = const [],
}) {
  final out = BytesBuilder();
  void u8(int v) => out.addByte(v & 0xFF);
  void u16(int v) {
    out.addByte(v & 0xFF);
    out.addByte((v >> 8) & 0xFF);
  }

  void u32(int v) {
    for (var i = 0; i < 4; i++) {
      out.addByte((v >> (8 * i)) & 0xFF);
    }
  }

  void i64(int v) {
    for (var i = 0; i < 8; i++) {
      out.addByte((v >> (8 * i)) & 0xFF);
    }
  }

  void f64(double v) {
    final bd = ByteData(8)..setFloat64(0, v, Endian.little);
    out.add(bd.buffer.asUint8List());
  }

  out.add(ascii.encode('MOTH'));
  u16(bytecodeVersion);
  u16(0); // flags

  u16(constants.length);
  for (final c in constants.items) {
    u8(c.tag);
    switch (c.tag) {
      case _tagInt:
        i64(c.value as int);
      case _tagDouble:
        f64(c.value as double);
      case _tagBool:
        u8((c.value as bool) ? 1 : 0);
      case _tagNull:
        break;
      case _tagString:
        final bytes = utf8.encode(c.value as String);
        u16(bytes.length);
        out.add(bytes);
    }
  }

  u16(natives.length);
  for (final n in natives) {
    u16(n.nameConst);
    u8(n.argc);
  }

  u16(globalCount);

  u16(classes.length);
  for (final c in classes) {
    u16(c.nameConst);
    u8(c.fieldNameConsts.length);
    for (final f in c.fieldNameConsts) {
      u16(f);
    }
    u16(c.methods.length);
    for (final (nameConst, fnIndex) in c.methods) {
      u16(nameConst);
      u16(fnIndex);
    }
    u16(c.ctor);
  }

  u16(functions.length);
  for (final f in functions) {
    u16(f.nameConst);
    u8(f.arity);
    u8(f.nlocals);
    u32(f.code.length);
    out.add(f.code);
  }

  u16(entry);
  u16(init);
  return out.toBytes();
}
