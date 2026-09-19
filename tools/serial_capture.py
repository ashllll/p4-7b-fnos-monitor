#!/usr/bin/env python3
"""抓串口日志（带时间戳），用于长跑与离线/恢复验证。

用法: python3 tools/serial_capture.py --seconds 720 --out logs/soak.log [--reset]
"""
import argparse, os, sys, time

import serial

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/tty.usbmodem5CF71088571")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--out", default="logs/serial.log")
    ap.add_argument("--reset", action="store_true", help="先做 DTR/RTS 复位再抓")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    if args.reset:
        ser.setDTR(False)
        ser.setRTS(True); time.sleep(0.2); ser.setRTS(False)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    fh = open(args.out, "w", encoding="utf-8")
    t0 = time.time()
    buf = b""
    print("capturing %s -> %s for %.0fs" % (args.port, args.out, args.seconds))
    while time.time() - t0 < args.seconds:
        chunk = ser.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, _, buf = buf.partition(b"\n")
            text = line.decode("utf-8", "replace").rstrip("\r")
            stamped = "[%7.2f] %s" % (time.time() - t0, text)
            fh.write(stamped + "\n")
            fh.flush()
            print(stamped)
    if buf:
        fh.write(buf.decode("utf-8", "replace"))
    fh.close()
    ser.close()
    print("done")

if __name__ == "__main__":
    main()
