#!/usr/bin/env python3
"""PPM(P6) → PNG，纯标准库（不引入 PIL）。用法：ppm2png.py <dir>"""
import sys, zlib, struct
from pathlib import Path


def read_ppm(p: Path):
    data = p.read_bytes()
    if not data.startswith(b"P6"):
        raise SystemExit(f"{p}: not a P6 PPM")
    # header: P6 <w> <h> <max>\n
    fields, i = [], 2
    while len(fields) < 3:
        while data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while data[i : i + 1] != b"\n":
                i += 1
            continue
        j = i
        while not data[j : j + 1].isspace():
            j += 1
        fields.append(int(data[i:j]))
        i = j
    i += 1
    w, h, _ = fields
    return w, h, data[i : i + w * h * 3]


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
    n = 0
    for ppm in sorted(d.glob("*.ppm")):
        w, h, rgb = read_ppm(ppm)
        png = ppm.with_suffix(".png")
        write_png(png, w, h, rgb)
        n += 1
    print(f"ppm2png: {n} file(s) → {d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
