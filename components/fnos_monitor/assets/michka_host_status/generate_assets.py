#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-4.0
"""Render the self-contained Michka SVGs and emit LVGL 9 image maps.

Uses only Python's standard library and macOS Quick Look (qlmanage).  The SVGs
are rendered at 576x576, then 32x32 supersample blocks are area-averaged to 18px.
"""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "michka_host_status.c"
SIZE = 18
SUPERSAMPLE = 576
SCALE = SUPERSAMPLE // SIZE
ONLINE = (0x36, 0xE0, 0x7A)
OFFLINE = (0xFF, 0x6B, 0x6B)
FACE = (0x14, 0x14, 0x14)


def png_rgba(path: Path) -> tuple[int, int, bytearray]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"not a PNG: {path}")
    pos = 8
    width = height = depth = color_type = interlace = None
    compressed = bytearray()
    while pos < len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        kind = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += length + 12
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk
            )
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break
    if (width, height, depth, color_type, interlace) != (SUPERSAMPLE, SUPERSAMPLE, 8, 6, 0):
        raise ValueError("Quick Look must produce a non-interlaced 576x576 RGBA PNG")

    stride = width * 4
    raw = zlib.decompress(compressed)
    if len(raw) != height * (stride + 1):
        raise ValueError("unexpected PNG scanline length")
    pixels = bytearray(height * stride)
    previous = bytearray(stride)
    src = 0
    for y in range(height):
        filter_type = raw[src]
        src += 1
        row = bytearray(raw[src : src + stride])
        src += stride
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            up = previous[x]
            upper_left = previous[x - 4] if x >= 4 else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xFF
            elif filter_type == 2:
                row[x] = (row[x] + up) & 0xFF
            elif filter_type == 3:
                row[x] = (row[x] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else up if pb <= pc else upper_left
                row[x] = (row[x] + predictor) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        pixels[y * stride : (y + 1) * stride] = row
        previous = row
    return width, height, pixels


def fit_opacity(pixel: tuple[int, int, int], foreground: tuple[int, int, int],
                background: tuple[int, int, int]) -> tuple[float, int]:
    channel = max(range(3), key=lambda c: abs(foreground[c] - background[c]))
    delta = foreground[channel] - background[channel]
    amount = 0.0 if delta == 0 else (pixel[channel] - background[channel]) / delta
    amount = min(1.0, max(0.0, amount))
    error = max(
        abs(pixel[c] - round(background[c] + amount * (foreground[c] - background[c])))
        for c in range(3)
    )
    return amount, error


def unmatte(width: int, height: int, pixels: bytearray,
            foreground: tuple[int, int, int],
            background_color: tuple[int, int, int] | None = None) -> bytearray:
    """Recover alpha against Quick Look's white matte; skull details sit on red."""
    if len(pixels) != width * height * 4:
        raise ValueError("unexpected RGBA image size")
    out = bytearray(width * height * 4)
    for i in range(0, len(pixels), 4):
        observed = tuple(pixels[i : i + 3])
        if pixels[i + 3] != 255:
            raise ValueError("expected Quick Look's opaque white thumbnail background")
        amount, error = fit_opacity(observed, foreground, (255, 255, 255))
        chosen = (error, foreground, round(amount * 255))
        if background_color is not None:
            ink_amount, ink_error = fit_opacity(observed, FACE, background_color)
            if ink_error < error:
                ink_rgb = tuple(
                    round(background_color[c] * (1.0 - ink_amount) + FACE[c] * ink_amount)
                    for c in range(3)
                )
                chosen = (ink_error, ink_rgb, 255)
        error, rgb, alpha = chosen
        if error > 3:
            raise ValueError(f"raster pixel did not match the source palette (error={error})")
        out[i : i + 3] = bytes(rgb if alpha else (0, 0, 0))
        out[i + 3] = alpha
    return out


def downsample(pixels: bytearray) -> list[int]:
    """Area-average in premultiplied space; return AARRGGBB words."""
    result: list[int] = []
    block_area = SCALE * SCALE
    row_stride = SUPERSAMPLE * 4
    for y in range(SIZE):
        for x in range(SIZE):
            alpha_sum = red_sum = green_sum = blue_sum = 0
            for sy in range(y * SCALE, (y + 1) * SCALE):
                start = sy * row_stride + x * SCALE * 4
                for sx in range(SCALE):
                    i = start + sx * 4
                    r, g, b, a = pixels[i : i + 4]
                    alpha_sum += a
                    red_sum += r * a
                    green_sum += g * a
                    blue_sum += b * a
            alpha = round(alpha_sum / block_area)
            if alpha:
                red = round(red_sum / alpha_sum)
                green = round(green_sum / alpha_sum)
                blue = round(blue_sum / alpha_sum)
            else:
                red = green = blue = 0
            result.append((alpha << 24) | (red << 16) | (green << 8) | blue)
    return result


def render() -> dict[str, list[int]]:
    with tempfile.TemporaryDirectory(prefix="michka-render-", dir=HERE) as temp:
        tempdir = Path(temp)
        outdir = tempdir / "out"
        outdir.mkdir()
        sources = []
        for source, name in (("pacman-online.svg", "online.svg"),
                             ("skull-offline.svg", "offline.svg")):
            target = tempdir / name
            target.write_bytes((HERE / source).read_bytes())
            sources.append(target)
        subprocess.run(
            ["qlmanage", "-t", "-s", str(SUPERSAMPLE), "-o", str(outdir), *map(str, sources)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        result = {}
        for name, color, background in (
            ("online", ONLINE, None),
            ("offline", OFFLINE, OFFLINE),
        ):
            width, height, rgba = png_rgba(outdir / f"{'online' if name == 'online' else 'offline'}.svg.png")
            clean = unmatte(width, height, rgba, color, background)
            result[name] = downsample(clean)
        return result


def c_source(images: dict[str, list[int]]) -> str:
    lines = [
        "/* SPDX-License-Identifier: CC-BY-NC-4.0",
        " * Derived Michka host-status raster assets; see LICENSE and README.md.",
        " * Pixel words are AARRGGBB and expanded to LVGL's byte-ordered color32 type.",
        " */",
        '#include "michka_host_status.h"',
        "",
        "#include <stdint.h>",
        "",
        "#define MICHKA_ARGB32(v) { \\",
        "    .blue = (uint8_t)((v) & 0xFFu), \\",
        "    .green = (uint8_t)(((v) >> 8) & 0xFFu), \\",
        "    .red = (uint8_t)(((v) >> 16) & 0xFFu), \\",
        "    .alpha = (uint8_t)(((v) >> 24) & 0xFFu) \\",
        "}",
        "",
        "_Static_assert(sizeof(lv_color32_t) == 4U, \"LVGL ARGB8888 pixel layout changed\");",
        "",
    ]
    for name in ("online", "offline"):
        lines.append(f"static const lv_color32_t {name}_pixels[{SIZE * SIZE}] = {{")
        for y in range(SIZE):
            row = images[name][y * SIZE : (y + 1) * SIZE]
            lines.append("    " + ", ".join(f"MICHKA_ARGB32(0x{word:08X}u)" for word in row) + ",")
        lines.extend(("};", ""))
    for name in ("online", "offline"):
        lines.extend(
            (
                f"const lv_image_dsc_t michka_host_status_{name} = {{",
                "    .header = {",
                "        .magic = LV_IMAGE_HEADER_MAGIC,",
                "        .cf = LV_COLOR_FORMAT_ARGB8888,",
                "        .flags = 0U,",
                "        .w = MICHKA_HOST_STATUS_WIDTH,",
                "        .h = MICHKA_HOST_STATUS_HEIGHT,",
                "        .stride = MICHKA_HOST_STATUS_WIDTH * sizeof(lv_color32_t),",
                "        .reserved_2 = 0U,",
                "    },",
                f"    .data_size = sizeof({name}_pixels),",
                f"    .data = (const uint8_t *){name}_pixels,",
                "    .reserved = NULL,",
                "    .reserved_2 = NULL,",
                "};",
                "",
            )
        )
    lines.append("#undef MICHKA_ARGB32")
    return "\n".join(lines) + "\n"


def inspect_svg_sources() -> None:
    expected = {
        "pacman-online.svg": (
            'viewBox="0 0 100 100"',
            'fill="#36e07a"',
            'd="M50 50 L88.97 27.5 A45 45 0 1 0 88.97 72.5 Z"',
        ),
        "skull-offline.svg": (
            'viewBox="4.17 3.17 55.65 55.65"',
            'fill="#ff6b6b"',
            'fill="#141414"',
            'd="M32 6C44 6 53 15 53 27C53 35 49 41 44 44C43 49 42 52 40 54C37 56 27 56 24 54C22 52 21 49 20 44C15 41 11 35 11 27C11 15 20 6 32 6Z"',
            'cx="23" cy="28" r="6.5"',
            'cx="41" cy="28" r="6.5"',
            'd="M32 34l-4 8 8 0z"',
            'x="27" y="46" width="1.7" height="8"',
            'x="31.15" y="46" width="1.7" height="8"',
            'x="35.3" y="46" width="1.7" height="8"',
        ),
    }
    for name, fragments in expected.items():
        text = (HERE / name).read_text(encoding="utf-8")
        if any(fragment not in text for fragment in fragments):
            raise ValueError(f"{name} no longer matches the attributed source geometry/colors")
        if any(token in text.lower() for token in ("<script", "<text ", "href=", "url(")):
            raise ValueError(f"{name} must remain self-contained, with no external fonts or scripts")


def inspect_c(text: str) -> dict[str, list[int]]:
    header = (HERE / "michka_host_status.h").read_text(encoding="utf-8")
    if (f"#define MICHKA_HOST_STATUS_WIDTH  {SIZE}U" not in header or
            f"#define MICHKA_HOST_STATUS_HEIGHT {SIZE}U" not in header):
        raise ValueError("public image dimensions do not match the generated maps")
    if "LV_COLOR_FORMAT_ARGB8888" not in text or "MICHKA_HOST_STATUS_WIDTH * sizeof(lv_color32_t)" not in text:
        raise ValueError("LVGL image descriptor format or stride is missing")
    if "_Static_assert(sizeof(lv_color32_t) == 4U" not in text:
        raise ValueError("LVGL ARGB8888 pixel size is not checked")
    images = {}
    for name in ("online", "offline"):
        descriptor = re.search(
            rf"const lv_image_dsc_t michka_host_status_{name} = \{{(.*?)\n\}};",
            text,
            re.S,
        )
        expected_descriptor = (
            ".magic = LV_IMAGE_HEADER_MAGIC,",
            ".cf = LV_COLOR_FORMAT_ARGB8888,",
            ".w = MICHKA_HOST_STATUS_WIDTH,",
            ".h = MICHKA_HOST_STATUS_HEIGHT,",
            ".stride = MICHKA_HOST_STATUS_WIDTH * sizeof(lv_color32_t),",
            f".data_size = sizeof({name}_pixels),",
            f".data = (const uint8_t *){name}_pixels,",
        )
        if not descriptor or any(field not in descriptor.group(1) for field in expected_descriptor):
            raise ValueError(f"{name} LVGL descriptor fields do not match its pixel map")
        match = re.search(
            rf"static const lv_color32_t {name}_pixels\[{SIZE * SIZE}\] = \{{(.*?)\n\}};",
            text,
            re.S,
        )
        if not match:
            raise ValueError(f"{name} pixel map is missing or has the wrong dimensions")
        words = [int(value, 16) for value in re.findall(r"MICHKA_ARGB32\(0x([0-9A-Fa-f]{8})u\)", match.group(1))]
        if len(words) != SIZE * SIZE:
            raise ValueError(f"{name} map has {len(words)} pixels; expected {SIZE * SIZE}")
        alphas = [word >> 24 for word in words]
        bounds = [SIZE, SIZE, -1, -1]
        for i, word in enumerate(words):
            alpha = word >> 24
            red, green, blue = (word >> 16) & 255, (word >> 8) & 255, word & 255
            x, y = i % SIZE, i // SIZE
            if alpha:
                bounds = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x), max(bounds[3], y)]
            elif (red, green, blue) != (0, 0, 0):
                raise ValueError(f"{name} transparent pixel contains color data")
        if min(alphas) != 0 or max(alphas) != 255 or bounds[2] < 0:
            raise ValueError(f"{name} alpha channel or visible bounds are invalid")
        images[name] = words
        print(f"PASS {name}: {SIZE}x{SIZE}, stride={SIZE * 4}, alpha=0..255, bounds={tuple(bounds)}")
    green = [w for w in images["online"] if (w & 0xFFFFFF) == 0x36E07A and (w >> 24) == 255]
    red = [w for w in images["offline"] if (w & 0xFFFFFF) == 0xFF6B6B and (w >> 24) == 255]
    dark = [w for w in images["offline"] if (w & 0xFFFFFF) == 0x141414 and (w >> 24) == 255]
    if not green or not red or not dark:
        raise ValueError("online green or offline skull color detail is missing")
    print(f"PASS palette: online green={len(green)} opaque px, offline red={len(red)}, face={len(dark)}")
    return images


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate the generated C maps without rerendering")
    args = parser.parse_args()
    inspect_svg_sources()
    if args.check:
        inspect_c(OUTPUT.read_text(encoding="utf-8"))
        return
    source = c_source(render())
    OUTPUT.write_text(source, encoding="utf-8")
    inspect_c(source)
    print(f"Wrote {OUTPUT.name}")


if __name__ == "__main__":
    main()
