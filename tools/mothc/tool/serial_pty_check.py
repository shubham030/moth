#!/usr/bin/env python3
"""Harness for serial_pty_check.dart: opens a pty, echoes everything written
to the master straight back, and runs the Dart side against the slave. This
is what gives the FFI termios layer automated coverage — open flags, raw
mode, cflag normalization and binary transparency all fail loudly here.
Run from tools/mothc (make serial-test does)."""
import os
import pty
import subprocess
import sys
import threading


def main():
    master, slave = pty.openpty()
    slave_path = os.ttyname(slave)

    def echo():
        try:
            while True:
                data = os.read(master, 256)
                if not data:
                    return
                os.write(master, data)
        except OSError:
            pass

    threading.Thread(target=echo, daemon=True).start()
    r = subprocess.run(
        ["dart", "run", "tool/serial_pty_check.dart", slave_path],
        timeout=120)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
