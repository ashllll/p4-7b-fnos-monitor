#!/usr/bin/env python3
"""Reject purple regressions; the user explicitly allows cyan and usage blue."""
import re
import sys
from pathlib import Path

from ppm2png import read_ppm

ROOT = Path(__file__).resolve().parents[2]


def forbidden(red, green, blue, tolerance=0):
    # Cyan is approved by the photos, blue by the occupancy-state requirement.
    # Purple has red and blue both above green. RGB565 channels may
    # round independently by up to 8 levels in a rendered/antialiased pixel.
    return min(red, blue) > green + tolerance


def main():
    assert not forbidden(63, 169, 245) and forbidden(180, 124, 222)
    assert not forbidden(168, 191, 141) and not forbidden(240, 240, 240)
    assert not forbidden(128, 216, 216) and not forbidden(142, 212, 156)
    assert not forbidden(0, 0, 255) and forbidden(255, 0, 255)
    source = (ROOT / "components/fnos_monitor/ui_kit/uk_theme.c").read_text()
    presets = re.findall(r"static const uk_pal_t (P_\w+) = \{(.*?)\n\};", source, re.S)
    selectable = set(re.findall(r'\{\s*"[^"]+",\s*&(?P<name>P_\w+)', source))
    assert presets and {name for name, _ in presets} == selectable, "Check every selectable preset"
    for name, body in presets:
        for token, value in re.findall(r"\.(\w+)\s*=\s*0x([0-9A-Fa-f]{6})", body):
            rgb = tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))
            assert not forbidden(*rgb), f"{name}.{token}: purple token"
    total = 0
    for argument in sys.argv[1:]:
        frames = sorted(Path(argument).rglob("*.ppm"))
        assert frames, f"No rendered frames: {argument}"
        for frame in frames:
            width, height, rgb = read_ppm(frame)
            assert (width, height) == (1024, 600), f"Unexpected screen: {frame}"
            bad = sum(forbidden(r, g, b, 8) for r, g, b in zip(rgb[::3], rgb[1::3], rgb[2::3]))
            assert bad == 0, f"{frame}: {bad} purple pixels"
        total += len(frames)
    print(f"PASS: all {len(presets)} presets; {total} rendered frames without purple (blue/cyan allowed)")


if __name__ == "__main__":
    main()
