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

    # 打开端口时就把两条控制线钉在"正常启动"电平：pyserial 默认 dtr=True/rts=True，
    # 对 ESP 的 USB-Serial-JTAG 就是 IO0=低 + EN=低；关端口时 EN 先放、IO0 后放，
    # 芯片在 EN 上升沿采到 IO0=低 ⇒ **进下载模式**停在 `waiting for download`。
    # 这是"抓完一次日志，板子就不跑了"的根因（会被误读成固件坏了）。
    ser = serial.Serial(args.port, args.baud, timeout=0.2)
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
    # 收工时拉一次复位。注意：靠 DTR/RTS 复位**不可靠** —— 本机实测拉完经常停在
    # `boot:0x7 (DOWNLOAD)`，而那种状态下 rst 仍报 POWERON、只有整机断电才能出来。
    # 需要"确定在跑应用"时，用 esptool 复位：
    #   python3 -m esptool --port <port> --after hard-reset chip-id
    # 再立刻抓串口。下面这段只是"聊胜于无"的收尾。
    # pyserial 打开/关闭端口会翻 DTR/RTS，
    # 而 ESP32-P4 的 USB-Serial-JTAG 把它们当 BOOT/EN —— 抓完日志不拉这一下，
    # 板子会停在 `rst:0x1 (POWERON),boot:0x7 (DOWNLOAD(...))  waiting for download`：
    # 应用根本没在跑，串口上只看得到重复的 ROM 头，而"轮询失败"的日志是停机前留下的。
    # 曾因此把"板子停在下载模式"误判成"固件轮询坏了"，查了很久。
    try:
        hard_reset()
    except Exception:
        pass
    ser.close()
    print("done (已复位回运行模式)")

if __name__ == "__main__":
    main()
