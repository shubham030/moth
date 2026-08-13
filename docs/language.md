---
title: Language
sidebar_position: 2
slug: /language
---

# The language

moth runs a subset of Dart. Everything below is real Dart — the same code
compiles and runs on the Dart SDK, which is how the test suite checks that
moth agrees with Dart itself.

## What works today

**Types:** `int` (64-bit), `double`, `bool`, `null`.

**Variables** — `var`, `final`, or an explicit type. Scoped to their block.

```dart
var count = 0;
final limit = 10;
int step = 2;
```

**Strings** — literals, `+`, interpolation, and `==` comparing text rather
than identity. Garbage collected.

```dart
print('temp $tempC C, fan ${running ? "on" : "off"}');
print('device at address $addr');
```

**Lists** — literals, indexing, `.length`, `add`, `removeLast`, `clear`, and
`for-in`. Garbage collected, and they may nest.

```dart
var window = [12, 7, 30];
window.add(19);
for (final v in window) {
  total += v;
}
print('avg ${total ~/ window.length}');
```

**Imports** — `import 'other.dart';` with a relative path. Declarations from
every imported file share one namespace, so a class can extend one declared
elsewhere. Import cycles are fine; each file is loaded once. `package:` and
`dart:` imports are not supported.

**Closures** — function values, lambdas, and functions passed around or kept
in lists. A lambda written inside a method captures `this`, so it can reach
that object's fields and methods.

```dart
button.onTap(() {
  count += 1;          // a field, reached through the captured `this`
  refresh();
});
```

A function stored in a field is callable like a method — `button.handler()`
works, resolving methods first and then fields, as Dart does.

Capturing a **local** of the enclosing function is not supported yet, and is
a compile error rather than a silent copy. Use a top-level variable or a
field — both are reachable from any closure.

**Classes** — fields with initializers, one constructor (with `this.x`
parameters), methods, and implicit `this`. Also supported: the ternary
`a ? b : c` and the null assertion `x!` (which passes through, since moth
does not check nullability).

**Single inheritance** with `extends`, including method overriding. An
inherited method that calls an overridden one lands on the override, as in
Dart. Duck typing also works — any two classes with the same method name are
interchangeable, with or without a shared base type.

Not supported: `implements`, mixins, and extending a class that declares a
constructor (there is no way to chain to it yet). Each is a compile error.

```dart
class DigitalPin {
  int number;
  bool state = false;

  DigitalPin(this.number) {
    pinOutput(number);
  }

  void toggle() {
    state = !state;
    digitalWrite(number, state);
  }
}
```

**Top-level variables** — shared by every function in the file, initialized
before `main` runs. Locals shadow them, as in Dart.

Unlike Dart, initialization is **eager and in declaration order**, not lazy.
Dart runs a top-level initializer on first use, so `var a = b + 1; var b = 2;`
prints 3; moth evaluates `a` first and traps on the null `b`. It fails loudly
rather than silently, but order your declarations so each only depends on
earlier ones.

```dart
final ledPin = 38;      // pin map and tuning, visible everywhere
var pressCount = 0;     // state that outlives any one function call
```

**Functions** — top-level, positional parameters, recursion, expression bodies.

```dart
int fib(int n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

int add(int a, int b) => a + b;
```

**Operators** — `+ - * / ~/ %`, `== != < <= > >=`, `&& || !`, `& | ^ << >> ~`,
`++ --`, and compound assignment (`+=`, `~/=`, `|=`, `<<=`, …). `&&` and `||`
short-circuit.

Bitwise operators matter more here than in app code — register masks and
combining sensor bytes are routine:

```dart
var reading = (highByte << 8) | lowByte;   // two I2C registers into one value
if ((status & 0x08) != 0) { }              // test a status flag
flags |= 0x10;                             // set a bit
```

Two Dart details moth reproduces exactly, because they trip people up:

- `/` always produces a `double`. `7 / 2` is `3.5`, not `3`. Use `~/` for
  integer division.
- `%` is Euclidean — the result is never negative. `-7 % 3` is `2`, and
  `7 % -3` is `1`.

**Control flow** — `if`/`else`, `while`, counting `for`, `break`, `continue`,
`return`.

```dart
for (var i = 0; i < 10; i++) {
  if (i == 3) continue;
  if (i == 8) break;
  print(i);
}
```

## Named parameters

```dart
int area({int w = 1, int h = 1}) => w * h;

area(h: 4, w: 3);   // order does not have to match the declaration
area(w: 3);         // h takes its default
```

They work wherever the compiler can see the declaration — a **constructor**
or a **top-level function**. Matching happens at compile time, so the VM
still executes an ordinary positional call and named arguments cost nothing
at run time.

A method call cannot use them. A method is dispatched on its receiver, whose
type is not known while compiling the call, so there is no parameter list to
match against; moth says so rather than guessing.

This is what lets a widget tree read as a tree:

```dart
Container(
  color: 0xFF0E0E12,
  padding: 20,
  onTap: () => setState(() => count += 1),
  child: Column(
    mainAxisAlignment: mainAxisCenter,
    spacing: 12,
    children: [
      Text('tapped $count times', style: TextStyle(fontSize: 20)),
      Divider(thickness: 2, width: 180),
    ],
  ),
)
```

Defaults must be constant, as in Dart — `const []` for an empty list.

## Cascades

`..` configures an object without naming it repeatedly. The target is
evaluated once, each section runs against it, and the expression's value is
the target — so a widget tree nests instead of unrolling into a list of
assignments to temporaries:

```dart
Box()
  ..color = 0xFF000000
  ..pad = 24
  ..kids = [
    Text()..value = '14:32'..size = 72,
    Text()..value = 'FRI 8 AUG'..size = 20,
  ]
```

Assignments and method calls both work as sections. This is what gives a
`build()` method the shape a Flutter developer expects; named parameters,
which would let it read `Box(color: black, kids: [...])`, are still to come.

## Getters and setters

A property can be computed rather than stored, and assigning to one can do
work:

```dart
class Thermo {
  int raw = 0;

  int get celsius => raw ~/ 10;
  set celsius(int c) {
    raw = c * 10;
  }

  bool get isFreezing => celsius <= 0;
}

var t = Thermo();
t.celsius = 25;      // runs the setter
print(t.isFreezing); // runs the getter
```

They compile to ordinary methods sharing the property's name; the VM tells a
getter from a setter by how many values it takes. Reading a property tries the
object's fields first and only then an accessor, so a plain field can later
become a getter without touching any caller.

This is what makes [package:moth's hardware API](hardware.md) worth using:
`led.value = true` really does drive the pin.

## What does not work yet

| Feature | Milestone |
| --- | --- |
| Maps | planned |
| Static members, named constructors | planned |
| `async` / `await`, `Future` | needs an event loop; see below |
| Networking — WiFi, sockets, HTTP | not started |
| Mixins, extensions, records | not planned for v1 |
| Generics | not planned for v1 — type annotations like `List<int>` are accepted and erased, never enforced |

## No async, and why

There is no event loop on the device, so there is nothing for a `Future` to
complete on. `async`, `await` and `Future` are rejected at compile time rather
than half-supported:

```
main.dart:1:26: async functions are not supported yet
  Future<int> twice(int n) async {
                           ^
  hint: moth has no event loop, so there is nothing for a Future to complete
        on — write it synchronously, and use millis() to spread slow work
        across frames
```

A program owns its own loop, so waiting is explicit — you call `delay()`, or
you check `millis()` and do a slice of work per frame:

```dart
var nextRead = 0;
while (true) {
  final now = millis();
  if (now >= nextRead) {
    nextRead = now + 1000;
    print(sensor.value);   // once a second, without blocking the frame
  }
  pumpFrame(16);
  delay(16);
}
```

That is the whole concurrency story today: one thread, one loop, no
preemption. It is a real limitation, not a simplification — a driver that
needs to wait on an interrupt has to be written in C behind a built-in.

## Strictness worth knowing about

**Conditions must be booleans.** There is no truthiness, exactly as in Dart:

```dart
while (1) { }   // error: condition must be a bool
while (true) { }
```

This is deliberate. `while (1)` is the most common reflex carried over from C,
and catching it at compile time is kinder than debugging it on a device.

**Errors point at the source, with a way forward:**

```
blink.dart:5:3: 'digitalWrite' takes 2 arguments, but got 1
    digitalWrite(led);
    ^
```

## How much fits

A blink program is 136 bytes of bytecode. The demo with a user-defined
function, arithmetic and a GPIO loop is 185 bytes. The VM itself is roughly
200 KB of flash and a few KB of RAM, so program size is rarely what limits you.
