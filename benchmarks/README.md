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

ESP32-S3 numbers: not yet measured (board was disconnected). Expect roughly
an order of magnitude lower — 240MHz versus ~4GHz, and PSRAM rather than
cache-resident memory.

## Reading the last row

The rebuild+diff case is the one M3 depends on: allocate a tree of
widget-sized objects, then walk it comparing properties. At ~857k node-ops
per second on the host, a 30-node rebuild costs about 35µs — so even a 10x
slowdown on device leaves a full-screen rebuild well inside a frame.

That is the number that decides whether `setState` can go through the tree
on every state change. It says yes, with room. Measure again on hardware
before relying on it.
