#!/usr/bin/env python3
"""PPM(P6) → PNG，纯标准库（不引入 PIL）。用法：ppm2png.py <dir>

审计 low 项：这个脚本是"验收图"的最后一环，而它过去对坏输入是**静默**的：
  * 头部读完但像素数据一个字节都没有（`P6\n1 1\n255\n`）时返回 `(1,1,b"")`，
    照样写出一个 1×1 的 PNG —— 看起来"转换成功"，其实那张图根本不存在；
  * 头部字段在文件末尾被截断时，内层 `while not data[j:j+1].isspace()` 会在 EOF 处
    **死循环**（`b"".isspace()` 永远是 False，j 一直涨）；
  * 目录里一个 `.ppm` 都没有时打印 `0 file(s)` 并 **return 0** ——
    空目录与"84 张全部转换成功"退出码一样。
现在：坏输入一律抛 PpmError 并以非零码收场；零样本目录算失败；maxval≠255 直接拒绝
（旧代码把 16 位样本当 8 位读，图会静默错位）。
"""
import sys
import zlib
import struct
from pathlib import Path


class PpmError(Exception):
    """PPM 结构不合法（头部字段缺失/截断、像素数据长度不符等）。"""


MAX_DIM = 20000


def read_ppm(p: Path):
    data = p.read_bytes()
    n = len(data)
    if not data.startswith(b"P6"):
        raise PpmError(f"{p}: 不是 P6 PPM（开头是 {data[:2]!r}）")
    fields, i = [], 2
    while len(fields) < 3:
        while i < n and data[i : i + 1].isspace():
            i += 1
        if i >= n:
            raise PpmError(f"{p}: 头部还没读满 3 个字段（w h maxval）文件就结束了")
        if data[i : i + 1] == b"#":
            nl = data.find(b"\n", i)
            if nl < 0:
                raise PpmError(f"{p}: 注释没有换行结尾，头部被截断")
            i = nl + 1
            continue
        j = i
        while j < n and not data[j : j + 1].isspace():
            j += 1
        if j >= n:
            # 这里就是旧代码死循环的地方：字段读到最后也没有分隔符
            raise PpmError(f"{p}: 头部字段被截断（末尾是 {data[i : i + 16]!r}）")
        try:
            fields.append(int(data[i:j]))
        except ValueError:
            raise PpmError(f"{p}: 头部字段不是整数：{data[i:j]!r}")
        i = j
    w, h, maxval = fields
    if w <= 0 or h <= 0:
        raise PpmError(f"{p}: 尺寸非法 {w}x{h}")
    if w > MAX_DIM or h > MAX_DIM:
        raise PpmError(f"{p}: 尺寸 {w}x{h} 超出合理范围（>{MAX_DIM}）")
    if maxval != 255:
        raise PpmError(f"{p}: maxval={maxval}，本工具只支持 8 位样本（255）")
    start = i + 1  # maxval 之后的一个空白字节
    need = w * h * 3
    got = n - start
    if got != need:
        raise PpmError(f"{p}: 像素数据 {got} 字节，按 {w}x{h} 需要 {need} 字节（截断或多出）")
    return w, h, data[start : start + need]


def write_png(p: Path, w: int, h: int, rgb: bytes):
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride : (y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    p.write_bytes(png)


def main() -> int:
    d = Path(sys.argv[1] if len(sys.argv) > 1 else "out")
    if not d.is_dir():
        print(f"ppm2png: 目录不存在：{d} —— 先跑渲染", file=sys.stderr)
        return 2
    files = sorted(d.glob("*.ppm"))
    if not files:
        # "0 file(s)" 曾经是 return 0：空目录与全部转换成功退出码一样（审计 low 项）
        print(f"ppm2png: {d} 里没有 .ppm 样本（0 个）—— 别把空目录当成转换成功", file=sys.stderr)
        return 2
    ok, failed = 0, 0
    for ppm in files:
        try:
            w, h, rgb = read_ppm(ppm)
            write_png(ppm.with_suffix(".png"), w, h, rgb)
            ok += 1
        except PpmError as exc:
            failed += 1
            print(f"ppm2png: {exc}", file=sys.stderr)
    if failed:
        print(f"ppm2png: {ok} 张转换成功、{failed} 张坏样本 → {d}", file=sys.stderr)
        return 1
    print(f"ppm2png: {ok} file(s) → {d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
