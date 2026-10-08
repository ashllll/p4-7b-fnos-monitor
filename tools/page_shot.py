#!/usr/bin/env python3
"""实机逐页拍照：串口切页 → 手机拍照 → 打印 PNG 路径。

用法: python3 tools/page_shot.py --page 4 [--boot 22] [--settle 3]

为什么需要它：这块板上没有触摸自动化（GT911 要人点），而实机验收恰恰要看**每一页**
在真实数据下的自适应结果。固件里为此加了串口命令 `page <0-4>`（只置标志，换页仍然
发生在 LVGL 任务里，见 fnos_ui_request_page），这个脚本负责"发命令 → 等数据 → 拍照"。

两个坑（都踩过）：
  1. pyserial 打开端口会把 DTR/RTS 拉起来，对 ESP32-P4 的 USB-Serial-JTAG 就是
     IO0=低+EN=低；**关闭端口**时电平先后释放，芯片会停在 `boot:0x7 (DOWNLOAD)`。
     所以这里开完端口立刻把两条线钉成"正常运行"电平，并且**整个拍照过程都握着端口**
     ——一关端口板子就复位，页就退回第 0 页了。
  2. 复位后串口输入缓冲里可能留着上一次的半行（ROM 阶段的杂散字节），直接发命令会变成
     `不认识的命令：(乱码)page 4`。先发一个空行冲掉，再发真命令。

拍照用 PATH 上的 capture-board（可通过 FNOS_CAPTURE_BOARD 配置）（手机相机预览截图）。看见图才算数：
本脚本只负责把图拍下来并打印路径，**判读必须由调用者 read_image 亲眼看**。
"""
import argparse
import json
import os
import subprocess
import sys
import time

import serial

CAPTURE = os.environ.get("FNOS_CAPTURE_BOARD", "capture-board")


def main(void=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("FNOS_SERIAL_PORT"))
    ap.add_argument("--page", type=int, required=True, help="0=总览 1=存储 2=网络 3=系统 4=温度")
    ap.add_argument("--boot", type=float, default=22.0, help="复位后等 Wi-Fi + 首次采集的秒数")
    ap.add_argument("--settle", type=float, default=3.0, help="切页后等动画与重排的秒数")
    args = ap.parse_args()
    if not args.port:
        ap.error("set --port or FNOS_SERIAL_PORT to the identified board serial port")

    ser = serial.Serial(args.port, 115200, timeout=0.3)
    ser.dtr = False          # IO0 = 高 → 正常启动而不是下载模式
    ser.rts = False          # EN  = 高 → 放开复位
    try:
        print("等待开机 + 首次采集（%.0fs）…" % args.boot, flush=True)
        time.sleep(args.boot)
        ser.reset_input_buffer()
        ser.write(b"\n")                      # 冲掉可能残留的半行
        ser.flush()
        time.sleep(0.4)
        ser.reset_input_buffer()
        ser.write(b"page %d\n" % args.page)
        ser.flush()
        time.sleep(args.settle)
        reply = ser.read(8192).decode("utf-8", "replace")
        ok = ("切到第 %d 页" % args.page) in reply
        for line in reply.splitlines():
            if "页" in line or "不认识" in line:
                print("设备: " + line.strip())
        if not ok:
            print("!! 没在串口回显里看到切页确认，照片可能还是上一页", file=sys.stderr)
        print("拍照中（端口保持打开，避免复位）…", flush=True)
        out = subprocess.run([CAPTURE], capture_output=True, text=True, timeout=180)
        tail = out.stdout.strip().splitlines()[-1] if out.stdout.strip() else "{}"
        info = json.loads(tail)
        print("PAGE=%d IMAGE=%s resumed_camera=%s" % (args.page, info.get("image"), info.get("resumed_camera")))
        if not ok:
            return 2
        return 0
    finally:
        ser.close()          # 关闭会让板子复位回第 0 页：这是本脚本的已知副作用


if __name__ == "__main__":
    sys.exit(main())
