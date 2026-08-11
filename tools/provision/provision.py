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

# The default ESP-IDF partition table, which ui/esp-s3 uses.
NVS_OFFSET = "0x9000"
NVS_SIZE = "0x6000"


def find_port(explicit):
    if explicit:
        return explicit
    candidates = glob.glob("/dev/cu.usbmodem*")
    if len(candidates) == 1:
        return candidates[0]
    sys.exit(f"provision: pass --port; found {candidates or 'no usbmodem ports'}")


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

        subprocess.run([sys.executable, gen, "generate", creds_csv, image,
                        NVS_SIZE], check=True, cwd=tmp)
        subprocess.run(["esptool.py", "--port", port, "write_flash",
                        NVS_OFFSET, image], check=True)

    print(f"\nprovision: credentials for '{args.ssid}' written. "
          "Reset the board; it prints its push address once connected.")


if __name__ == "__main__":
    main()
