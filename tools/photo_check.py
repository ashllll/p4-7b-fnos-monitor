#!/usr/bin/env python3
"""判读 board-camera 照片是否真拍到开发板（定量自检）。

判据（实测基线）：拍到板子 ≈ meanL 40 左右、彩色像素占比 ≥2%；
拍到桌面/挪走了 ≈ meanL 80 左右、彩色 <0.1%。

用法：python3 tools/photo_check.py 照片.png [更多.png ...]
"""
import io, struct, sys, zlib


def decode_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a png"
    pos, idat, w, h, ctype = 8, b"", 0, 0, 0
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
            ctype = body[9]
        elif typ == b"IDAT":
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * ch
    prev = bytearray(stride)
    out = []
    p = 0
    for y in range(h):
        ft = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if ft == 1:
            for x in range(ch, stride):
                line[x] = (line[x] + line[x - ch]) & 0xFF
        elif ft == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ft == 3:
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ft == 4:
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                b = prev[x]
                c = prev[x - ch] if x >= ch else 0
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        prev = line
        out.append(bytes(line))
    return w, h, ch, out


def stats(path, step=3):
    w, h, ch, rows = decode_png(path)
    tot = colored = 0
    lum = 0
    for y in range(0, h, step):
        row = rows[y]
        for x in range(0, w, step):
            if ch >= 3:
                r, g, b = row[x * ch], row[x * ch + 1], row[x * ch + 2]
            else:
                r = g = b = row[x * ch]
            lum += (r * 299 + g * 587 + b * 114) / 1000
            if max(r, g, b) - min(r, g, b) > 18:
                colored += 1
            tot += 1
    return w, h, lum / tot, 100.0 * colored / tot


for p in sys.argv[1:]:
    w, h, ml, cp = stats(p)
    verdict = "拍到板子" if (ml < 60 and cp >= 1.5) else ("拍到桌面/其它" if cp < 0.5 else "存疑")
    print("%-52s %dx%d meanL=%.1f color=%.2f%% -> %s" % (p.split("/")[-1], w, h, ml, cp, verdict))
