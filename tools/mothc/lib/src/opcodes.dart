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
  static const loadGlobal = 0x0A;
  static const storeGlobal = 0x0B;

  static const add = 0x10;
  static const sub = 0x11;
  static const mul = 0x12;
  static const div = 0x13;
  static const idiv = 0x14;
  static const mod = 0x15;
  static const neg = 0x16;
  static const band = 0x17;
  static const bor = 0x18;
  static const bxor = 0x19;
  static const shl = 0x1A;
  static const shr = 0x1B;
  static const bnot = 0x1C;
  static const toStringOp = 0x1D;

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

  static const newList = 0x50;
  static const indexGet = 0x51;
  static const indexSet = 0x52;

  static const newInstance = 0x57;
  static const getProp = 0x58;
  static const setProp = 0x59;
  static const invoke = 0x5A;
  static const closure = 0x5B;
  static const callValue = 0x5C;
}

/// Host functions a program may call, with their argument counts. The VM
/// resolves these by name at load time; a board that doesn't register one
/// rejects the blob up front rather than failing mid-run.
/// Names mirror Arduino's core API where one exists, so an Arduino tutorial
/// translates line for line. See docs/arduino-parity.md.
const kNatives = <String, int>{
  // output
  'print': 1,

  // timing
  'delay': 1,
  'delayMicroseconds': 1,
  'millis': 0,
  'micros': 0,

  // digital I/O
  'pinOutput': 1,
  'pinInput': 1,
  'pinInputPullup': 1,
  'digitalWrite': 2,
  'digitalRead': 1,

  // analog I/O
  'analogRead': 1,
  'analogWrite': 2,

  // sound
  'tone': 2,
  'noTone': 1,

  // random
  'randomSeed': 1,
  'random': 1,

  // I2C — single-register access covers most sensors and needs no heap
  'i2cBegin': 2,
  'i2cPing': 1,
  'i2cWriteReg': 3,
  'i2cReadReg': 2,

  // UART
  'uartBegin': 4,
  'uartWrite': 2,
  'uartAvailable': 1,
  'uartRead': 1,

  // Display — the moth_render backend contract (docs/BACKEND.md). Flat and
  // numeric because it is the VM boundary; package:moth wraps it in classes.
  'uiWidth': 0,
  'uiHeight': 0,
  'uiRoot': 0,
  'uiCreate': 1, // kind: 0 box, 1 label, 2 image, 3 slider, 4 switch
  'uiDestroy': 1,
  'uiAttach': 3, // parent, child, index (-1 appends)
  'uiDetach': 1,
  'uiSetNum': 3, // node, prop, double
  'uiSetInt': 3, // node, prop, int (colours, enums)
  'uiSetText': 3, // node, prop, text
  'uiAnimate': 6, // node, prop, from, to, ms, easing
  'uiTick': 1,
  'uiCommit': 0, // true when the frame actually repainted
  'uiPoll': 0, // node * 8 + kind, or -1 when the queue is empty
  'uiEventValue': 0,
  'uiFrameOf': 2, // node, 0..3 -> x, y, w, h
  'uiSafeArea': 1, // 0..3 -> x, y, w, h of the always-visible rectangle
};
