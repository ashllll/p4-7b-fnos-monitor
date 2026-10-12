#!/usr/bin/env python3
"""对 board-camera 照片做外观统计自检；不能证明设备身份、固件或数据新鲜度。

判据（实测基线）：拍到板子 ≈ meanL 40 左右、彩色像素占比 ≥2%；
拍到桌面/挪走了 ≈ meanL 80 左右、彩色 <0.1%。

用法：python3 tools/photo_check.py 照片.png [更多.png ...]
"""
import struct, sys, zlib

MAX_BYTES = 64 * 1024 * 1024     # 照片不该超过这个；超了多半是传错了文件
MAX_DECODED_BYTES = 64 * 1024 * 1024
MAX_PIXELS = 64 * 1024 * 1024    # w*h 上限，防止伪造 IHDR 撑爆内存


def fail(msg):
    """判读工具不能靠 assert：断言在 -O 下会消失，栈回溯也说不清是哪张图坏了。"""
    sys.stderr.write("photo_check: %s\n" % msg)
    raise SystemExit(2)


def decode_png(path):
    try:
        with open(path, "rb") as source:
            data = source.read(MAX_BYTES + 1)
    except OSError as exc:
        fail("读不了 %s：%s" % (path, exc))
    if len(data) > MAX_BYTES:
        fail("%s 超过 %d 字节" % (path, MAX_BYTES))
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        fail("%s 不是 PNG（magic=%r）" % (path, data[:8]))
    pos, idat = 8, bytearray()
    w = h = bit_depth = ctype = interlace = None
    ended = seen_idat = idat_closed = False
    while pos + 8 <= len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        if pos + 12 + ln > len(data):
            fail("%s 的 %s 块越界（长度 %d，剩 %d 字节）" % (path, typ, ln, len(data) - pos - 8))
        body = data[pos + 8:pos + 8 + ln]
        crc = struct.unpack(">I", data[pos + 8 + ln:pos + 12 + ln])[0]
        if zlib.crc32(typ + body) & 0xffffffff != crc:
            fail("%s 的 %s 块CRC错误" % (path, typ))
        if pos == 8 and typ != b"IHDR":
            fail("%s 首块不是IHDR" % path)
        if typ == b"IHDR":
            if w is not None:
                fail("%s 重复IHDR" % path)
            if ln != 13:
                fail("%s 的 IHDR 长度是 %d（应为 13）" % (path, ln))
            w, h, bit_depth, ctype, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            if compression != 0 or filtering != 0:
                fail("%s 不支持的压缩/滤波方法" % path)
        elif typ == b"IDAT":
            if idat_closed:
                fail("%s 的IDAT块不连续" % path)
            seen_idat = True
            idat.extend(body)
        elif typ == b"IEND":
            if ln != 0:
                fail("%s IEND非空" % path)
            ended = True
            pos += 12
            break
        else:
            if seen_idat:
                idat_closed = True
            if typ == b"PLTE":
                if not ln or ln % 3 or ln > 768:
                    fail("%s PLTE长度非法" % path)
            elif not typ or not (typ[0] & 32):
                fail("%s 不支持的关键块 %r" % (path, typ))
        pos += 12 + ln
    if not ended or pos != len(data):
        fail("%s 缺IEND或存在尾随数据" % path)
    if w is None:
        fail("%s 没有 IHDR" % path)
    # 这几条以前完全没查（审计 M16）：16 位深会按 8 位解读、隔行图会整幅错位、
    # 调色板图直接 KeyError 崩掉 —— 三种情况下算出来的 meanL/彩色占比都是垃圾，
    # 而工具照样打印一个"拍到板子 / 拍到桌面"的结论。
    if bit_depth != 8:
        fail("%s 是 %d 位深，本工具只解 8 位" % (path, bit_depth))
    if interlace != 0:
        fail("%s 用了隔行（Adam7 Interlace=%d），解码会整幅错位" % (path, interlace))
    if ctype not in (0, 2, 4, 6):
        fail("%s 的 color type=%s，只支持灰度/真彩（不含调色板 PNG）" % (path, ctype))
    if w == 0 or h == 0:
        fail("%s 的尺寸是 %dx%d" % (path, w, h))
    if w * h > MAX_PIXELS:
        fail("%s 是 %dx%d，超过 %d 像素上限" % (path, w, h, MAX_PIXELS))
    if not idat:
        fail("%s 没有 IDAT 数据" % path)
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    stride = w * ch
    expected = h * (stride + 1)
    if expected > MAX_DECODED_BYTES:
        fail("%s 解码字节数超过预算 %d" % (path, MAX_DECODED_BYTES))
    try:
        decoder = zlib.decompressobj()
        raw = decoder.decompress(idat, expected + 1)
    except zlib.error as exc:
        fail("%s 的 IDAT 解不开：%s" % (path, exc))
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        fail("%s 解压长度/流结尾非法（实际%d，应为%d）" % (path, len(raw), expected))
    prev = bytearray(stride)
    out = []
    p = 0
    for y in range(h):
        ft = raw[p]; p += 1
        if ft > 4:
            fail("%s 第 %d 行的滤波类型是 %d（0..4 之外）" % (path, y, ft))
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
    if not isinstance(step, int) or step <= 0:
        fail("采样步长必须为正整数")
    w, h, ch, rows = decode_png(path)
    tot = colored = 0
    lum = 0.0
    for y in range(0, h, step):
        row = rows[y]
        for x in range(0, w, step):
            off = x * ch
            # 透明像素不参与统计（审计 M16）：带 alpha 的图上，"看不见"的区域
            # 以前照样算进亮度和彩色占比，meanL 会被算低、被判成"拍到板子"。
            if ch in (2, 4) and row[off + ch - 1] < 8:
                continue
            if ch >= 3:
                r, g, b = row[off], row[off + 1], row[off + 2]
            else:
                r = g = b = row[off]
            lum += (r * 299 + g * 587 + b * 114) / 1000
            if max(r, g, b) - min(r, g, b) > 18:
                colored += 1
            tot += 1
    if tot == 0:
        # 全透明或没有可用采样点；尺寸小于step仍会采到(0,0)。
        fail("%s 没有可统计的像素（%dx%d，采样步长 %d）" % (path, w, h, step))
    return w, h, lum / tot, 100.0 * colored / tot


def main():
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    worst = 0
    for p in sys.argv[1:]:
        w, h, ml, cp = stats(p)
        verdict = "拍到板子" if (ml < 60 and cp >= 1.5) else ("拍到桌面/其它" if cp < 0.5 else "存疑")
        if verdict != "拍到板子":
            worst = 1
        print("%-52s %dx%d meanL=%.1f color=%.2f%% -> %s" % (p.split("/")[-1], w, h, ml, cp, verdict))
    return worst


if __name__ == "__main__":
    sys.exit(main())
