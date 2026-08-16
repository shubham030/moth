// Pins and buses, as objects.
//
// The built-ins underneath are flat functions — digitalWrite(38, true) —
// because that is the shape a VM boundary wants. This is the layer people
// write against, and it is ordinary Dart: a pin is a thing with a value you
// read or assign, not a number you pass to a function that may be expecting a
// different one.
//
//   final led = OutputPin(38);
//   led.value = true;
//
// Reading and writing go through getters and setters, so assigning to `value`
// really does drive the pin. Every class configures its hardware in its
// constructor, so there is no separate setup step to forget.

// The host functions underneath. Imported for this file's own use and
// re-exported so a program importing this library sees them too — the Dart
// analyzer's imports are not transitive, even though moth's global scope is.
import 'natives.dart';
export 'natives.dart';

/// A pin the program drives.
class OutputPin {
  final int number;

  bool _value = false;

  OutputPin(this.number) {
    pinOutput(number);
  }

  /// What the pin is currently driving. Remembered rather than read back,
  /// because an output pin cannot be read on every board.
  bool get value => _value;

  set value(bool on) {
    _value = on;
    digitalWrite(number, on);
  }

  /// Flips the pin, and returns what it became.
  bool toggle() {
    value = !value;
    return value;
  }
}

/// A pin the program reads.
class InputPin {
  final int number;

  /// [pullUp] holds the pin high until something pulls it down — the usual
  /// wiring for a button between the pin and ground. With it, an unpressed
  /// button reads true, which is why [isPressed] exists.
  InputPin(this.number, bool pullUp) {
    if (pullUp) {
      pinInputPullup(number);
    } else {
      pinInput(number);
    }
  }

  bool get value => digitalRead(number);

  /// True while a button wired to ground is held down.
  bool get isPressed => !value;
}

/// A pin read as a voltage rather than a yes or no.
class AnalogPin {
  final int number;

  AnalogPin(this.number);

  /// The raw reading, 0 to 4095 on an ESP32.
  int get value => analogRead(number);

  /// The same reading as a fraction from 0.0 to 1.0. Usually the more useful
  /// one: it keeps its meaning if the hardware's resolution changes.
  double get fraction => value / 4095.0;

  /// The reading mapped onto [low]..[high] — the usual thing wanted from a
  /// potentiometer, without every program rewriting the arithmetic. An
  /// inverted range works too: scaled(100, 0) counts down.
  int scaled(int low, int high) {
    // Add-half-then-truncate rounds correctly only for non-negative values,
    // and ~/ truncates toward zero — so an inverted range read a step high
    // across half of every step. Doing the arithmetic on a positive span and
    // flipping afterwards keeps both directions honest.
    if (high >= low) return low + (fraction * (high - low) + 0.5) ~/ 1;
    return low - (fraction * (low - high) + 0.5) ~/ 1;
  }
}

/// A pin driven with a square wave — dimming an LED, driving a motor.
class PwmPin {
  final int number;

  int _duty = 0;

  PwmPin(this.number) {
    pinOutput(number);
  }

  /// How much of each cycle the pin is on, 0 to 255. Values outside that are
  /// clamped rather than rejected, so a sensor reading can be fed in directly.
  int get duty => _duty;

  set duty(int level) {
    var clamped = level;
    if (clamped < 0) clamped = 0;
    if (clamped > 255) clamped = 255;
    _duty = clamped;
    analogWrite(number, clamped);
  }

  /// The same, as a fraction from 0.0 to 1.0.
  double get level => _duty / 255.0;

  set level(double fraction) {
    // `~/ 1` rather than .round(): moth has no methods on numbers yet, and
    // adding a half before truncating rounds the same way.
    duty = (fraction * 255 + 0.5) ~/ 1;
  }
}

/// A buzzer or speaker on one pin.
class Buzzer {
  final int number;

  Buzzer(this.number);

  /// Starts a tone and leaves it playing until [stop].
  void play(int frequencyHz) {
    tone(number, frequencyHz);
  }

  void stop() {
    noTone(number);
  }

  /// Plays for a fixed time, then stops.
  void beep(int frequencyHz, int milliseconds) {
    play(frequencyHz);
    delay(milliseconds);
    stop();
  }
}

/// A hobby servo on one pin.
///
/// A servo is not told an angle. It is told a pulse width, repeated at 50Hz,
/// and it holds whatever position that width means to it — which is why
/// [writeMicroseconds] is the real control and [write] is a convention on top.
class Servo {
  final int pin;

  Servo(this.pin) {
    servoAttach(pin);
  }

  /// The pulse width in microseconds. The host clamps to 500..2500, so a
  /// value from a knob can be fed straight in without stalling the servo
  /// against its own stop.
  void writeMicroseconds(int us) {
    servoMicroseconds(pin, us);
  }

  /// [degrees] 0..180 mapped onto 1000..2000us — the range nearly every servo
  /// understands, and out-of-range degrees are clamped rather than refused.
  ///
  /// Where 0 and 180 actually point varies by servo: a cheap one may not reach
  /// either end, a continuous-rotation one ignores the idea of an angle
  /// entirely. Use [writeMicroseconds] when the exact position matters.
  void write(int degrees) {
    var clamped = degrees;
    if (clamped < 0) clamped = 0;
    if (clamped > 180) clamped = 180;
    writeMicroseconds(1000 + clamped * 1000 ~/ 180);
  }
}

/// The I2C bus, shared by every sensor and display on those two wires.
class I2c {
  final int sda;
  final int scl;

  I2c(this.sda, this.scl) {
    i2cBegin(sda, scl);
  }

  /// True when a device answers at [address] — the first thing to check when
  /// a sensor seems dead.
  bool has(int address) => i2cPing(address);

  int readRegister(int address, int register) => i2cReadReg(address, register);

  bool writeRegister(int address, int register, int value) =>
      i2cWriteReg(address, register, value);

  /// [count] bytes from consecutive registers, read in one transaction. A
  /// sensor that reports a value across several registers updates between
  /// reads, so fetching them one at a time can return half of one reading and
  /// half of the next. Empty when the device did not answer — check that
  /// before indexing, since there is no -1 to check for here.
  List<int> readBytes(int address, int register, int count) =>
      i2cReadBytes(address, register, count);

  /// Writes [bytes] to consecutive registers starting at [register]. Each
  /// item is truncated to a byte. False when the device did not answer.
  bool writeBytes(int address, int register, List<int> bytes) =>
      i2cWriteBytes(address, register, bytes);
}

/// One device on an I2C bus, so its address is written once rather than at
/// every call.
class I2cDevice {
  final I2c bus;
  final int address;

  I2cDevice(this.bus, this.address);

  bool get isPresent => bus.has(address);

  int read(int register) => bus.readRegister(address, register);

  bool write(int register, int value) =>
      bus.writeRegister(address, register, value);

  /// Empty when the device did not answer.
  List<int> readBytes(int register, int count) =>
      bus.readBytes(address, register, count);

  bool writeBytes(int register, List<int> bytes) =>
      bus.writeBytes(address, register, bytes);
}

/// A serial port, for a GPS, a modem, or another board.
class Uart {
  final int port;

  Uart(this.port, int tx, int rx, int baud) {
    uartBegin(port, tx, rx, baud);
  }

  /// Sends one byte.
  void writeByte(int byte) {
    uartWrite(port, byte);
  }

  /// Sends [text] one byte at a time. Characters outside ASCII are sent as
  /// their UTF-16 code units truncated to a byte, which is what the wire
  /// can carry — send bytes yourself if that matters.
  void write(String text) {
    for (var i = 0; i < text.length; i++) {
      uartWrite(port, text.codeUnitAt(i) & 0xFF);
    }
  }

  /// The next byte, or -1 when nothing is waiting.
  int read() => uartRead(port);

  /// How many bytes are waiting.
  int get available => uartAvailable(port);

  bool get hasData => available > 0;
}

/// Named integers that outlive the program — a volume setting, a boot count,
/// which screen was open last.
///
/// On a board these live in NVS flash and survive a reboot and a reflash. In
/// the simulator they live in memory and vanish with the process, so a program
/// that remembers something still runs on a desk; it just starts fresh.
///
/// Keys are 1 to 15 characters. That limit is NVS's, not a choice made here.
class Prefs {
  /// The value stored under [key], or [fallback] when nothing is. A key over
  /// 15 characters cannot have been stored, so it reads back as [fallback]
  /// too — there is no separate "invalid" answer to handle.
  int getInt(String key, int fallback) => prefsGetInt(key, fallback);

  /// False when the key is invalid or the store is full. Worth checking on
  /// the writes you care about: afterwards a save that failed looks exactly
  /// like one that never happened.
  bool setInt(String key, int value) => prefsSetInt(key, value);
}
