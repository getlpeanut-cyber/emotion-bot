#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
One-shot full flash: bootloader + partition table + firmware + assets.

Why: the assets partition was enlarged from 4MB to 6MB, so the NEW
partition table (partitions.bin @ 0x8000) MUST be flashed together with
everything else. Flashing only firmware.bin / assets keeps the old 4MB
partition table, and the last few .eaf files fall outside the mapped
region -> black screen.

Usage:
  python tools/flash_all.py --port COM3
  python tools/flash_all.py              # auto-detect serial port
"""
import argparse
import os
import subprocess
import sys

CHIP = "esp32s3"
BUILD_DIR = ".pio/build/esp32-s3-devkitc-1"
ASSETS_BIN = "d:/project_entire/project/emotion_bot/data/emote-assets_new.bin"
ASSETS_OFFSET = 0x650000

# [offset, path]
FILES = [
    (0x0,        os.path.join(BUILD_DIR, "bootloader.bin")),
    (0x8000,     os.path.join(BUILD_DIR, "partitions.bin")),
    (0x10000,    os.path.join(BUILD_DIR, "firmware.bin")),
    (ASSETS_OFFSET, ASSETS_BIN),
]


def find_pio_esptool():
    tool_dir = os.path.expanduser("~/.platformio/packages/tool-esptoolpy")
    esptool_py = os.path.join(tool_dir, "esptool.py")
    pio_py = os.path.expanduser("~/.platformio/penv/Scripts/python.exe")
    if os.path.exists(esptool_py) and os.path.exists(pio_py):
        return [pio_py, esptool_py]
    return [sys.executable, "-m", "esptool"]


def detect_port():
    try:
        import serial.tools.list_ports
    except ImportError:
        raise SystemExit("missing pyserial: pip install pyserial")
    ports = serial.tools.list_ports.comports()
    usb = [p for p in ports if "BTHENUM" not in (p.hwid or "")]
    if not usb:
        raise SystemExit("no USB serial port found; pass --port")
    for p in usb:
        if "VID:PID=303A" in (p.hwid or "").upper():
            return p.device
    return usb[0].device


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None, help="serial port (auto-detect if omitted)")
    ap.add_argument("--baud", default=921600, type=int)
    ap.add_argument("--no-reset", action="store_true",
                    help="chip is already in download mode; skip the auto-reset")
    args = ap.parse_args()

    for _, path in FILES:
        if not os.path.exists(path):
            raise SystemExit("missing file: %s (run `pio run` first)" % path)

    port = args.port or detect_port()
    esptool = find_pio_esptool()
    cmd = esptool + ["--chip", CHIP, "--port", port, "--baud", str(args.baud)]
    if args.no_reset:
        cmd += ["--before", "no_reset"]
    cmd += ["write_flash"]
    for offset, path in FILES:
        cmd += [hex(offset), path]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True)
    print("DONE. Reset the board to run.")


if __name__ == "__main__":
    main()
