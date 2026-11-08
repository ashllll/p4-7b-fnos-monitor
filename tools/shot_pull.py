#!/usr/bin/env python3
"""把设备上的一页"截"下来：串口发 shot，收 SHOT-BEGIN/END 之间的 base64，写 PPM。

板上没有快门，macOS 也不给相机权限（capture-board 也装不上），要像素证据只能让设备
自己把 LVGL 渲染结果吐出来。固件侧见 fnos_wifi_cli.c:cmd_shot()。

一次快照 300 KB → base64 400 KB → 115200 下约 40 秒。中途被打断（Ctrl-C、killed），
设备还会继续吐完剩下的数据，这时再发 shot 只会把命令排队、越积越多。所以每次动作前
先读到"安静 2 秒"把残渣排干，再发命令。

读数据必须自己攒字节再按 \n 切（pyserial 的 readline 超时会返回半行，实测把一行
512 字符切成 430，看着像设备把行撕了）。

用法：
    python3 tools/shot_pull.py --port /dev/cu.usbmodem5CF71088571 --page 0 --out tools/preview/out/board/p0.ppm
    python3 tools/shot_pull.py --port ... --overlay pair   --out .../pair.ppm
    python3 tools/shot_pull.py --port ... --page 1 --overlay detail --out .../detail.ppm
"""
import argparse
import base64
import sys
import time

import serial  # pyserial；用带 pyserial 的 python（本机是 idf 的 venv）

B64 = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")


def drain_quiet(ser, quiet=2.0, max_s=60.0):
    """读到连续 quiet 秒没有新字节为止，丢掉内容。"""
    end = time.time() + max_s
    last = time.time()
    while time.time() < end:
        if ser.read(4096):
            last = time.time()
        elif time.time() - last > quiet:
            return


def read_frame(ser, timeout):
    buf = bytearray()
    started = False
    w = h = 0
    # 没见到 SHOT-BEGIN 只等 60 秒就换一轮：命令可能落在开机/切页的空档里丢掉，
    # 而设备日志每 0.1 秒一行，会一直刷新 last，于是"静默 20 秒"这个出口永远不触发。
    deadline = time.time() + min(timeout, 60.0)
    last = time.time()
    while time.time() < deadline:
        d = ser.read(16384)
        if not d:
            if time.time() - last > 20.0:
                return None
            continue
        last = time.time()
        buf += d
        if not started:
            k = buf.find(b"SHOT-BEGIN")
            if k < 0:
                if len(buf) > 65536:
                    del buf[:len(buf) - 64]
                continue
            e = buf.find(b"\n", k)
            if e < 0:
                continue
            try:
                w, h = (int(x) for x in bytes(buf[k + 11:e]).split())
            except ValueError:
                del buf[:e + 1]
                continue
            del buf[:e + 1]
            started = True
            deadline = time.time() + timeout      # 帧头到手后按整帧给时间（40 秒量级）
        k = buf.find(b"SHOT-END")
        if k >= 0:
            return w, h, bytes(buf[:k])
    return None


def pull_once(ser, page, overlay, out, timeout):
    drain_quiet(ser)
    if page is not None:
        # 板子刚启动时命令会丢，切页又是幂等的：发两遍，中间各排一次残渣。
        for _ in range(2):
            ser.write(("page %d\n" % page).encode())
            time.sleep(1.5)
            drain_quiet(ser, quiet=1.0, max_s=20.0)
    # 覆盖层（配对卡 / 详情抽屉）只能点开：shot pair|detail 让板子先摊开再截。
    ser.write(("shot %s\n" % overlay).encode() if overlay else b"shot\n")
    got = read_frame(ser, timeout)
    if not got:
        print("没收到完整帧（%s）" % ("覆盖层" if overlay else "第 %s 页" % page), file=sys.stderr, flush=True)
        return False
    w, h, body = got
    lines = [l.strip() for l in body.split(b"\n")]
    lines = [l for l in lines if l]
    # 512 字节一片 → 512 个 base64 字符；中间行长度不对就是被撕了，整帧重来。
    for i, line in enumerate(lines):
        if i + 1 < len(lines) and len(line) != 512:
            print("第 %d 行长度 %d（应为 512）" % (i, len(line)), file=sys.stderr)
            return False
        if any(chr(c) not in B64 for c in line):
            print("第 %d 行有杂质：%r" % (i, line[:40]), file=sys.stderr)
            return False
    raw = base64.b64decode(b"".join(lines))
    if len(raw) != w * h * 2:
        print("帧长不对：%d != %d" % (len(raw), w * h * 2), file=sys.stderr)
        return False
    with open(out, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        for i in range(0, len(raw), 2):
            c = raw[i] | (raw[i + 1] << 8)
            f.write(bytes((((c >> 11) & 0x1F) * 255 // 31,
                           ((c >> 5) & 0x3F) * 255 // 63,
                           (c & 0x1F) * 255 // 31)))
    print("PAGE=%s IMAGE=%s %dx%d" % (page, out, w, h))
    return True


def pull(port, page, overlay, out, timeout):
    ser = serial.Serial(port, 115200, timeout=0.3)
    try:
        time.sleep(0.4)
        for attempt in range(1, 5):
            if pull_once(ser, page, overlay, out, timeout):
                return
            print("第 %d 次没拿到完整帧，重试" % attempt, file=sys.stderr)
        raise SystemExit("四轮都没拿到完整帧")
    finally:
        ser.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--page", type=int, default=None)
    ap.add_argument("--overlay", choices=["pair", "detail"], default=None,
                    help="先摊开覆盖层再截（pair=配对卡；detail=当前页第一行的详情，配合 --page 1/2/3）")
    ap.add_argument("--out", required=True)
    ap.add_argument("--timeout", type=float, default=240.0)
    a = ap.parse_args()
    pull(a.port, a.page, a.overlay, a.out, a.timeout)


if __name__ == "__main__":
    main()
