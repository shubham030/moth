#!/usr/bin/env python3
"""Writes WiFi credentials into a moth board's NVS partition.

Credentials never touch the firmware image or the repo: this generates an NVS
partition image on the host and flashes it to the NVS partition alone. The app,
bootloader and partition table are untouched, and `idf.py flash` does not touch
NVS — so provisioning and reflashing never undo each other.

The password is asked for interactively (getpass) so it stays out of shell
history; --password exists for scripting but prefer the prompt.

Usage:
    python3 tools/provision/provision.py [--port /dev/cu.usbmodemXXXX] --ssid MyAP

Needs the ESP-IDF environment (esptool + nvs_partition_gen ship with it).
"""

import argparse
import csv
import getpass
import glob
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PARTITIONS = os.path.join(REPO, "ui", "esp-s3", "partitions.csv")


def nvs_geometry():
    """Reads the nvs partition's offset and size from the same file the
    firmware's build uses. Hardcoding them here once meant a resized table
    would have this script writing an image over whatever partition moved
    into the old offset — silently."""
    try:
        with open(PARTITIONS) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = [p.strip() for p in line.split(",")]
                if len(parts) >= 5 and parts[0] == "nvs":
                    if not parts[3] or not parts[4]:
                        sys.exit("provision: the nvs row in partitions.csv "
                                 "uses blank offset/size — fill them in "
                                 "explicitly so this script cannot guess")
                    return parts[3], parts[4]
    except OSError as e:
        sys.exit(f"provision: cannot read {PARTITIONS} — {e}")
    sys.exit(f"provision: no nvs row in {PARTITIONS}")


def find_port(explicit):
    if explicit:
        return explicit
    candidates = (glob.glob("/dev/cu.usbmodem*")      # macOS
                  + glob.glob("/dev/ttyUSB*")          # Linux, USB-serial
                  + glob.glob("/dev/ttyACM*"))         # Linux, CDC
    if len(candidates) == 1:
        return candidates[0]
    sys.exit(f"provision: pass --port; found {candidates or 'no serial ports'}")


def idf_tool(relpath):
    idf = os.environ.get("IDF_PATH")
    if not idf:
        sys.exit("provision: IDF_PATH not set — run the ESP-IDF export script "
                 "first (. ~/esp/esp-idf/export.sh)")
    tool = os.path.join(idf, relpath)
    if not os.path.exists(tool):
        sys.exit(f"provision: {tool} not found in this IDF")
    return tool


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="serial port; auto-detected if only one")
    ap.add_argument("--ssid", required=True)
    ap.add_argument("--password", help="prefer the interactive prompt; an "
                                       "empty string means an open network")
    args = ap.parse_args()

    password = args.password
    if password is None:
        password = getpass.getpass(f"WiFi password for '{args.ssid}' "
                                   "(empty for an open network): ")
    port = find_port(args.port)
    gen = idf_tool("components/nvs_flash/nvs_partition_generator/"
                   "nvs_partition_gen.py")

    # The CSV holds the password in plain text, so it lives only in a private
    # temp dir and is removed the moment the image is flashed.
    with tempfile.TemporaryDirectory(prefix="moth-provision-") as tmp:
        creds_csv = os.path.join(tmp, "creds.csv")
        image = os.path.join(tmp, "nvs.bin")
        with open(creds_csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["key", "type", "encoding", "value"])
            w.writerow(["moth", "namespace", "", ""])
            w.writerow(["wifi_ssid", "data", "string", args.ssid])
            w.writerow(["wifi_pass", "data", "string", password])

        nvs_offset, nvs_size = nvs_geometry()
        subprocess.run([sys.executable, gen, "generate", creds_csv, image,
                        nvs_size], check=True, cwd=tmp)
        # Through the IDF virtualenv's python, where esptool is guaranteed
        # importable — `sys.executable -m esptool` fails when this script is
        # launched with a system python, and the bare `esptool.py` name was
        # renamed in esptool v5. IDF's export script sets the env path.
        idf_env = os.environ.get("IDF_PYTHON_ENV_PATH")
        py = os.path.join(idf_env, "bin", "python") if idf_env else sys.executable
        subprocess.run([py, "-m", "esptool", "--port", port,
                        "write_flash", nvs_offset, image], check=True)

    print(f"\nprovision: credentials for '{args.ssid}' written. "
          "Reset the board; it prints its push address once connected.")


if __name__ == "__main__":
    main()
