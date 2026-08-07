/// Opcode numbers — must match vm/src/internal.h and docs/BYTECODE.md.
class Op {
  static const nop = 0x00;
  static const konst = 0x01;
  static const int8 = 0x02;
  static const pushTrue = 0x03;
  static const pushFalse = 0x04;
  static const pushNull = 0x05;
  static const pop = 0x06;
  static const dup = 0x07;
  static const load = 0x08;
  static const store = 0x09;

  static const add = 0x10;
  static const sub = 0x11;
  static const mul = 0x12;
  static const div = 0x13;
  static const idiv = 0x14;
  static const mod = 0x15;
  static const neg = 0x16;

  static const eq = 0x20;
  static const ne = 0x21;
  static const lt = 0x22;
  static const le = 0x23;
  static const gt = 0x24;
  static const ge = 0x25;
  static const not = 0x26;

  static const jump = 0x30;
  static const jumpIfFalse = 0x31;
  static const jumpIfFalseKeep = 0x32;
  static const jumpIfTrueKeep = 0x33;

  static const call = 0x40;
  static const native = 0x41;
  static const ret = 0x42;
  static const retNull = 0x43;
}

/// Host functions a program may call, with their argument counts. The VM
/// resolves these by name at load time; a board that doesn't register one
/// rejects the blob up front rather than failing mid-run.
const kNatives = <String, int>{
  'print': 1,
  'delay': 1,
  'millis': 0,
  'pinOutput': 1,
  'pinInput': 1,
  'pinInputPullup': 1,
  'digitalWrite': 2,
  'digitalRead': 1,
};
