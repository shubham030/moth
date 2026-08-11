# moth — working notes for Claude

Dart subset + Flutter-style widgets on ESP32-class microcontrollers.
Docs live in `docs/` (BACKEND.md is the render contract, BYTECODE.md the VM
contract, ROADMAP.md the state of the world). ADRs bind: never read or write
LVGL XML (license, ADR-002).

## Performance: what to track and how

The performance record lives in `docs/ROADMAP.md` (R3/R3a/R3b tables). The
current baseline on the ESP32-S3 466x466 board: **26.2ms a frame, 38.2fps**
on `examples/ui/frame_bench.dart`.

Rules, in order of how expensive they were to learn:

1. **fps numbers come from hardware only**: `make fps PORT=<port>`. That is
   end-to-end wall clock — VM rebuild, layout, paint, RGB565 convert, QSPI
   transfer landing on the panel. Host timings are not fps and are never
   quoted as fps.
2. **`make render-test` must stay green** (it runs inside `make test`). Its
   budgets are pixels visited per frame — deterministic on every machine. If
   a legitimate change needs a bigger budget, raise it in the same commit
   and say why in the commit message.
3. **Pure optimizations prove byte-identity**: render a probe scene against
   the previous commit (git worktree) and `cmp` framebuffers. Precision
   changes state their bound (the integer blend: ≤2/255, blended pixels
   only) and where it applies. Confirm the probe actually exercises the
   changed path — assert pixels > 0 before asserting what they are.
4. **Perf claims are measured before/after on the board, same benchmark.**
   A measured zero improvement means the mechanism is broken — instrument
   with the per-primitive split and find out, don't reason it away.

Checklist for reviewing render changes: `docs/PERF_REVIEW.md`. Read it
before touching `moth_render/src/paint.cpp`, `scene.cpp` (damage tracking),
`layout.cpp` (wrap/measure), `moth_render/esp-s3/main/panel.c`, or
`ui/src/moth_ui.c` — every item on it is a regression this repo has already
had once.

Profiling knobs (all off in normal builds, zero cost when off):
- `MR_PROFILE` — per-primitive us + pixel counters in moth_render; the
  embedder provides `mr_prof_now_us()`
- `MOTH_FRAME_PROFILE` — PHASES/SPLIT serial logging plus the `membench`
  PSRAM-floor microbench in `ui/esp-s3/main/app_main.c`
- `make fps` turns both on via `-DMOTH_FPSBENCH=1` in a separate `build-fps/`
  directory; it swaps `program.mothb` for the benchmark and restores it, but
  the *board* is left running the benchmark build — reflash after.

## Building and testing

- `make vm` — host build (mothrun, mothsim, render_perf_test, harness)
- `make test` — compiler goldens + rejections + render perf budgets
- `make ui F=examples/ui/watchface.dart` — run a UI program in a window
- ESP32-S3 app: `cd ui/esp-s3 && idf.py build flash` (IDF 5.4, export.sh
  first). `program.mothb` is the embedded Dart program — compile with
  `dart run tools/mothc/bin/mothc.dart <file> -o ui/esp-s3/main/program.mothb`.

## Hardware in this room

The dev board is a Waveshare ESP32-S3 AMOLED 1.75C (466x466 round, CO5300
panel, CST9217 touch, 16MB flash, 8MB PSRAM). Ports move between sessions —
identify a board by reading its app descriptor from flash (project name at
0x10000), never by port number alone.
