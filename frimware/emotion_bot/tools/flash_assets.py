#!/usr/bin/env python3
# -*- coding: gbk -*-
"""
把 emote-assets.bin 烧写到 flash 的 assets 分区（偏移 0x650000）。

用法:
  python tools/flash_assets.py --port COM3
  python tools/flash_assets.py              # 自动探测串口

依赖 pio 自带的 esptool。烧写前需先 `pio run` 生成分区表对应的 bootloader/分区，
否则 assets 分区不会被创建（分区表在首次 upload 时写入）。
"""
import argparse
import os
import subprocess
import sys
import glob

ASSETS_OFFSET = 0x650000
BIN = "d:/project_entire/project/emotion_bot/data/emote-assets_new.bin"
CHIP = "esp32s3"


def find_pio_esptool():
    # 用 pio 自带的 tool-esptoolpy（含 esptool.py 及其依赖）
    tool_dir = os.path.expanduser("~/.platformio/packages/tool-esptoolpy")
    esptool_py = os.path.join(tool_dir, "esptool.py")
    pio_py = os.path.expanduser("~/.platformio/penv/Scripts/python.exe")
    if os.path.exists(esptool_py) and os.path.exists(pio_py):
        return [pio_py, esptool_py]
    # 退回当前 python 的 esptool 模块
    return [sys.executable, "-m", "esptool"]


def detect_port():
    try:
        import serial.tools.list_ports
    except ImportError:
        raise SystemExit("缺少 pyserial，请先 `pip install pyserial`")
    ports = serial.tools.list_ports.comports()
    # 排除蓝牙串口（BTHENUM），只留真实 USB 串口
    usb = [p for p in ports if "BTHENUM" not in (p.hwid or "")]
    if not usb:
        raise SystemExit("未发现 USB 串口，请用 --port 指定")
    # 优先 Espressif(VID 303A) 的端口，即 ESP32-S3 的 USB-Serial-JTAG
    for p in usb:
        if "VID:PID=303A" in (p.hwid or "").upper():
            return p.device
    return usb[0].device


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None, help="串口(默认自动探测)")
    ap.add_argument("--bin", default=BIN, help="emote-assets.bin 路径")
    ap.add_argument("--offset", default=ASSETS_OFFSET, type=lambda s: int(s, 0))
    ap.add_argument("--baud", default=921600, type=int)
    args = ap.parse_args()

    if not os.path.exists(args.bin):
        raise SystemExit("找不到 %s" % args.bin)

    port = args.port or detect_port()
    esptool = find_pio_esptool()
    cmd = esptool + [
        "--chip", CHIP, "--port", port, "--baud", str(args.baud),
        "write_flash", hex(args.offset), args.bin,
    ]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
