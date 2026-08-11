#!/usr/bin/env python3
"""Measures moth's real frame rate on a connected ESP32-S3 board.

The number this reports is end-to-end wall clock: the Dart VM rebuilding the
widget tree, reconciliation, layout, paint, ARGB->RGB565 conversion, and the
QSPI transfer landing on the panel. Nothing on the host can stand in for it —
paint that is compute-bound on an M-series Mac is a different workload on a
240MHz Xtensa with PSRAM — which is why this script exists instead of a host
benchmark. Host-side regressions are caught by render_perf_test (pixel
budgets); this catches everything that test cannot see.

What it does:
  1. compiles examples/ui/frame_bench.dart with mothc
  2. swaps it in as ui/esp-s3/program.mothb (the original is restored after)
  3. builds with -DMOTH_FPSBENCH=1 into build-fps/ (the normal build dir is
     left alone) and flashes
  4. reads the serial log: MEMBENCH floors, PHASES, SPLIT, and the benchmark's
     own wall-clock repaint average
  5. fails if fps is below --min-fps

Usage:
    python3 tools/fpsbench/fpsbench.py --port /dev/cu.usbmodemXXXX
    make fps PORT=/dev/cu.usbmodemXXXX

Needs the ESP-IDF environment (idf.py on PATH — `. ~/esp/esp-idf/export.sh`)
and pyserial.
"""

import argparse
import glob
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
APP = REPO / "ui" / "esp-s3"
PROGRAM = APP / "main" / "program.mothb"
BENCH_DART = REPO / "examples" / "ui" / "frame_bench.dart"


def run(cmd, cwd=None):
    """Runs a command, echoing it; raises on failure with the output shown."""
    print(f"+ {' '.join(str(c) for c in cmd)}")
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True)


def find_port(explicit):
    if explicit:
        return explicit
    candidates = glob.glob("/dev/cu.usbmodem*")
    if len(candidates) == 1:
        return candidates[0]
    sys.exit(f"fpsbench: pass --port; found {candidates or 'no usbmodem ports'}")


def capture_serial(port, seconds):
    """Resets the board and returns log lines until the bench summary lands."""
    import serial  # deferred so --help works without pyserial

    lines = []
    with serial.Serial(port, 115200, timeout=1) as s:
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.1)
        s.setRTS(False)
        deadline = time.time() + seconds
        while time.time() < deadline:
            line = s.readline().decode("utf-8", "replace").rstrip()
            if not line:
                continue
            lines.append(line)
            if "repaints in" in line:  # summary printed; grab a little more
                deadline = min(deadline, time.time() + 5)
    return lines


def report(lines, min_fps):
    """Prints the interesting lines and returns the measured fps (or None)."""
    fps = None
    for line in lines:
        if any(k in line for k in ("MEMBENCH", "psram", "internal", "PHASES",
                                   "SPLIT", "repaints in")):
            print(line)
        m = re.search(r"repaints in \d+ms = ([\d.]+)ms each", line)
        if m:
            fps = 1000.0 / float(m.group(1))
    if fps is None:
        print("fpsbench: no benchmark summary seen on serial — wrong port, "
              "or the build/flash did not take")
        return None
    verdict = "PASS" if fps >= min_fps else "FAIL"
    print(f"\nfpsbench: {fps:.1f} fps (minimum {min_fps}) — {verdict}")
    return fps


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="serial port; auto-detected if only one")
    ap.add_argument("--min-fps", type=float, default=30.0,
                    help="fail below this (default 30)")
    ap.add_argument("--capture-seconds", type=float, default=60.0)
    args = ap.parse_args()

    if shutil.which("idf.py") is None:
        sys.exit("fpsbench: idf.py not on PATH — run the ESP-IDF export "
                 "script first (. ~/esp/esp-idf/export.sh)")
    port = find_port(args.port)

    bench_blob = REPO / "examples" / "ui" / "frame_bench.mothb"
    run(["dart", "run", "tools/mothc/bin/mothc.dart", BENCH_DART,
         "-o", bench_blob], cwd=REPO)

    backup = PROGRAM.read_bytes()
    try:
        PROGRAM.write_bytes(bench_blob.read_bytes())
        run(["idf.py", "-B", "build-fps", "-D", "MOTH_FPSBENCH=1",
             "build"], cwd=APP)
        run(["idf.py", "-B", "build-fps", "-p", port, "flash"], cwd=APP)
        fps = report(capture_serial(port, args.capture_seconds), args.min_fps)
    finally:
        PROGRAM.write_bytes(backup)

    print("\nNOTE: the board is still running the benchmark build. "
          "Reflash the real program with:  cd ui/esp-s3 && idf.py flash")
    sys.exit(0 if fps is not None and fps >= args.min_fps else 1)


if __name__ == "__main__":
    main()
