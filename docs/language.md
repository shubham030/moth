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

**Functions** — top-level, positional parameters, recursion, expression bodies.

```dart
int fib(int n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

int add(int a, int b) => a + b;
```

**Operators** — `+ - * / ~/ %`, `== != < <= > >=`, `&& || !`, `++ --`, and
compound assignment (`+=`, `~/=`, …). `&&` and `||` short-circuit.

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

## What does not work yet

| Feature | Milestone |
| --- | --- |
| Strings (`'hello'`, interpolation) | M1b |
| Lists, maps, `for-in` | M1b |
| Classes, methods, constructors | M1b |
| Closures and function values | M1b |
| `async` / `await`, `Future` | after M2 |
| Mixins, generics, extensions, records | not planned for v1 |
| `import` of other files | not planned for v1 |

All of the M1b items depend on one thing — a heap with a garbage collector —
so they land together.

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

A blink program is 130 bytes of bytecode. The demo with a user-defined
function, arithmetic and a GPIO loop is 185 bytes. The VM itself is roughly
200 KB of flash and a few KB of RAM, so program size is rarely what limits you.
