# Benchmarks

```
cmake --build build
dart run tools/mothc/bin/mothc.dart benchmarks/vm_bench.dart
./build/vm/mothrun benchmarks/vm_bench.mothb --quiet --real-time
```

`--real-time` matters: without it `millis()` is the simulator's virtual
clock, which only advances on `delay()`, and every measurement reads as 1ms.

## Results

**macOS, arm64, release build** — ops per second:

| workload | ops/sec |
| --- | --- |
| loop + integer add | 6.8M |
| function calls (`fib(22)`) | 8.1M |
| field writes | 7.7M |
| field reads | 6.3M |
| method calls | 4.2M |
| list index reads | 5.6M |
| string interpolation | 1.8M |
| **rebuild + diff, 30 nodes** | **857k** |

**ESP32-S3 @ 240MHz** — the numbers that actually matter:

| workload | ops/sec |
| --- | --- |
| loop + integer add | 168k |
| function calls (`fib(22)`) | 248k |
| field writes | 197k |
| field reads | 156k |
| method calls | 109k |
| list index reads | 142k |
| string interpolation | 17k |
| **rebuild + diff, 30 nodes** | **12k** |

That is roughly 30-70x slower than the host, not the 10x first guessed.

## Reading the last row

The rebuild+diff case is the one M3 depends on: allocate a tree of
widget-sized objects, then walk it comparing properties.

**On device a 30-node rebuild costs about 2.3ms** (35µs on the host). Against
a 16ms frame that leaves room, so `setState` going through the tree on every
change is sound — but with far less headroom than the host suggests. A few
hundred nodes, or a rebuild on every frame rather than every interaction,
would not fit.

The practical rule this implies is the one the architecture already states:
state changes go through the tree, continuous motion goes through the
renderer's own animation engine. String interpolation at 17k/sec is the
sharpest edge — building text in a hot rebuild path is what will cost.
