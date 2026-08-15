/// The host functions the VM provides, declared for the Dart analyzer.
///
/// These are `external` because that is exactly what they are: implemented
/// outside Dart, by the host (mothrun, mothsim, or the board's firmware).
/// mothc resolves them by name from its own table and skips these
/// declarations; the VM binds them at load time and refuses a program whose
/// board is missing one.
///
/// Declaring them buys real things: an editor that knows `analogRead`
/// exists, autocomplete, and — the reason this file was written — argument
/// and return types the analyzer checks. Without it every moth program was
/// a wall of "undefined function" and no type error could ever surface.
///
/// The signatures here are the contract. `natives_test.dart` asserts this
/// file and mothc's table describe the same set with the same arities, so
/// the two cannot drift.
///
/// `print` is deliberately absent: `dart:core` already declares it, and a
/// second declaration would shadow it for no gain.
library;

// ---- timing ---------------------------------------------------------------

/// Blocks for [ms] milliseconds.
external void delay(int ms);

/// Blocks for [us] microseconds.
external void delayMicroseconds(int us);

/// Milliseconds since boot.
external int millis();

/// Microseconds since boot.
external int micros();

// ---- digital I/O ----------------------------------------------------------

external void pinOutput(int pin);
external void pinInput(int pin);
external void pinInputPullup(int pin);

/// Sets an output pin. [value] is a real `bool`, not `1`/`0`.
external void digitalWrite(int pin, bool value);
external bool digitalRead(int pin);

// ---- analog I/O -----------------------------------------------------------

/// Raw ADC reading.
external int analogRead(int pin);

/// PWM duty cycle, 0..255.
external void analogWrite(int pin, int duty);

// ---- sound ----------------------------------------------------------------

external void tone(int pin, int hz);
external void noTone(int pin);

// ---- random ---------------------------------------------------------------

external void randomSeed(int n);

/// A pseudo-random int in 0..[max) — deterministic until [randomSeed].
external int random(int max);

// ---- I2C ------------------------------------------------------------------

external void i2cBegin(int sda, int scl);

/// True when a device answers at [addr].
external bool i2cPing(int addr);
external bool i2cWriteReg(int addr, int reg, int value);

/// The register's value, or -1 when the device did not answer.
external int i2cReadReg(int addr, int reg);

/// [n] bytes read from consecutive registers starting at [reg], in one
/// transaction — the shape a sensor's multi-byte reading arrives in. The list
/// is **empty** when the device did not answer: once every byte is a legal
/// value there is no -1 left to spare as a sentinel. [n] is clamped to 1..64
/// by the host.
external List<int> i2cReadBytes(int addr, int reg, int n);

/// Writes [bytes] to consecutive registers starting at [reg], each item
/// truncated to a byte. False when the device did not answer.
external bool i2cWriteBytes(int addr, int reg, List<int> bytes);

// ---- UART -----------------------------------------------------------------

external void uartBegin(int port, int tx, int rx, int baud);

/// Sends one byte.
external void uartWrite(int port, int byte);

/// Bytes waiting to be read.
external int uartAvailable(int port);

/// The next byte, or -1 when nothing is waiting.
external int uartRead(int port);

// ---- persistent storage ---------------------------------------------------
// Named ints that outlive the program: NVS flash on a board, memory in the
// simulator. Keys are NVS keys — 1 to 15 characters, and that limit is the
// hardware's, not a choice made here.

/// The int stored under [key], or [fallback] when there is none. A key longer
/// than 15 characters cannot have been stored, so it too reads as [fallback].
external int prefsGetInt(String key, int fallback);

/// Stores [value] under [key]. False when the key is invalid or the store is
/// full — worth checking, because a save that failed is indistinguishable
/// afterwards from one that never happened.
external bool prefsSetInt(String key, int value);

// ---- servo ----------------------------------------------------------------

/// Configures [pin] for 50Hz servo PWM. Call once, before any pulse.
external void servoAttach(int pin);

/// The pulse width that positions the horn, clamped to 500..2500 by the host
/// because a servo driven past its stop grinds against it.
external void servoMicroseconds(int pin, int us);

// ---- display --------------------------------------------------------------
// The moth_render backend contract (docs/BACKEND.md), flat and numeric
// because it is the VM boundary. The widget layer in widgets.dart is what
// you should be writing.

external int uiWidth();
external int uiHeight();

/// The root node, which spans the display and is owned by the backend.
external int uiRoot();

/// kind: 0 box, 1 label, 2 image, 3 slider, 4 switch, 5 arc.
external int uiCreate(int kind);
external void uiDestroy(int node);

/// index -1 appends.
external void uiAttach(int parent, int child, int index);
external void uiDetach(int node);

/// Sets a float property. Ints are accepted and widened, which is why the
/// value is `num`.
external void uiSetNum(int node, int prop, num value);

/// Sets an integer property — colours and enums.
external void uiSetInt(int node, int prop, int value);
external void uiSetText(int node, int prop, String value);

/// Starts a native animation; returns its id, or 0 when it could not start.
external int uiAnimate(
    int node, int prop, num from, num to, int durationMs, int easing);

/// Advances animations by [dtMs].
external void uiTick(int dtMs);

/// Lays out, paints and presents. True when the frame actually repainted.
external bool uiCommit();

/// The next event as `node * 8 + kind`, or -1 when the queue is empty.
external int uiPoll();

/// The value carried by the event [uiPoll] just returned.
external double uiEventValue();

/// A node's laid-out frame: [which] 0..3 selects x, y, w, h.
external int uiFrameOf(int node, int which);

/// The always-visible rectangle: [which] 0..3 selects x, y, w, h. On a round
/// panel this is the inscribed square.
external int uiSafeArea(int which);
