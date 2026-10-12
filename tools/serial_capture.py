#!/usr/bin/env python3
"""抓串口日志（带时间戳），用于长跑与离线/恢复验证。

用法: python3 tools/serial_capture.py --seconds 720 --out logs/soak.log [--reset]
仅 --reset 会主动发出一次复位；打开/关闭端口的驱动电平副作用需实机确认。
"""
import argparse, math, os, sys, time

MAX_PENDING = 1 << 20      # 单行最多攒 1 MiB，防止"没有换行"的日志把内存吃光


def hard_reset(ser):
    """尽力而为的 DTR/RTS 复位。**不可靠** —— 本机实测经常停在
    `boot:0x7 (DOWNLOAD)`；要确定性复位请用
      python3 -m esptool --port <port> --after hard-reset chip-id
    顺序很重要：先把 IO0 抬到高（正常启动），再放 EN。"""
    ser.rts = False    # IO0 = 高
    ser.dtr = True     # EN = 低（保持复位）
    time.sleep(0.1)
    ser.dtr = False    # EN 放高 ⇒ 采到 IO0=高，进应用而不是下载模式
    time.sleep(0.05)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("FNOS_SERIAL_PORT"))
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--out", default="logs/serial.log")
    ap.add_argument("--reset", action="store_true", help="先做 DTR/RTS 复位再抓")
    args = ap.parse_args()
    if not args.port:
        ap.error("set --port or FNOS_SERIAL_PORT to the identified board serial port")

    if not math.isfinite(args.seconds) or args.seconds <= 0 or args.baud <= 0:
        ap.error("--seconds must be finite and positive; --baud must be positive")
    import serial

    # Configure line states before open. Some OS/USB drivers may still glitch on
    # open/close; this tool cannot promise electrically reset-free observation.
    ser = serial.Serial(port=None, baudrate=args.baud, timeout=0.2)
    try:
        ser.rts = False
        ser.dtr = False
        ser.port = args.port
        ser.open()
        if args.reset:
            hard_reset(ser)
            print("已按 --reset 做了一次 DTR/RTS 复位（设备结果需另行验证）")
        os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as fh:
            capture(ser, fh, args.seconds, args.out)
    finally:
        ser.close()
    print("done")
    return 0


def capture(ser, fh, seconds, output_name):
    # monotonic：time.time() 会被 NTP/对时往前后拽，长跑日志的时间戳会跳，
    # 而这份时间戳正是 soak/离线恢复分析算"断了多久"的依据（审计 M16）。
    t0 = time.monotonic()
    buf = b""
    print("capturing %s -> %s for %.0fs" % (ser.port, output_name, seconds))
    while time.monotonic() - t0 < seconds:
        chunk = ser.read(4096)
        if not chunk:
            continue
        buf += chunk
        if len(buf) > MAX_PENDING:
            # 一直不出现换行的日志（比如二进制乱码）：整段落盘并记一笔，别无限攒。
            text = buf.decode("utf-8", "replace")
            stamped = "[%7.2f] %s" % (time.monotonic() - t0, text)
            fh.write(stamped + "\n")
            fh.flush()
            print(stamped)
            print("warning: %d 字节没有换行，已整段落盘" % len(buf))
            buf = b""
            continue
        while b"\n" in buf:
            line, _, buf = buf.partition(b"\n")
            text = line.decode("utf-8", "replace").rstrip("\r")
            stamped = "[%7.2f] %s" % (time.monotonic() - t0, text)
            fh.write(stamped + "\n")
            fh.flush()
            print(stamped)
    if buf:
        # 收工时残留的半行也要带时间戳：以前直接裸写，分析脚本按 "[  12.34] " 切时间，
        # 这一行会变成"没有时间戳的日志"，断流点的时长就算错了。
        stamped = "[%7.2f] %s" % (time.monotonic() - t0, buf.decode("utf-8", "replace"))
        fh.write(stamped + "\n")
        fh.flush()


if __name__ == "__main__":
    sys.exit(main())
