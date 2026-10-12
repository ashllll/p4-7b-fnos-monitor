#!/usr/bin/env python3
"""回归：验收图转换器对坏输入/空目录必须失败（审计 low 项 —— ppm2png 静默通过）

复核指出两处"看起来成功了"的假绿：
  1. `read_ppm()` 对只有头部、没有像素数据的 `P6\n1 1\n255\n` 返回 `(1,1,b"")`，
     于是**凭空**写出一个 1×1 的 PNG；头部字段在 EOF 处被截断时还会死循环。
  2. `main()` 对"目录里 0 个 .ppm"打印 `0 file(s)` 并 return 0 —— 与 84 张全部成功同码。
本回归直接喂这两种输入（外加截断 / 非 P6 / maxval=65535 / 尺寸非法），断言非零退出，
并用一张真样本做正向对照（必须真的产出 PNG，且 IHDR 宽高正确）。
用法：python3 tools/preview/test_ppm2png.py
"""
import io
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import ppm2png  # noqa: E402

fail = 0


def bad(msg):
    global fail
    fail = 1
    print(f"  ✗ {msg}")


def good(msg):
    print(f"  ✓ {msg}")


def run_main(d, extra=None):
    cmd = [sys.executable, str(HERE / "ppm2png.py"), str(d)]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    return p.returncode, p.stdout + p.stderr


def expect_raises(name, payload, why):
    with tempfile.TemporaryDirectory() as td:
        f = Path(td) / "x.ppm"
        f.write_bytes(payload)
        try:
            w, h, rgb = ppm2png.read_ppm(f)
        except ppm2png.PpmError:
            good(f"{name} → 被拒（{why}）")
            return
        except Exception as exc:  # 别的异常也算失败：说明没走"明确的拒绝"这条路
            bad(f"{name}：抛的是 {type(exc).__name__}: {exc}，不是 PpmError")
            return
        bad(f"{name}：居然被接受，返回 {w}x{h}，{len(rgb)} 字节像素（{why}）")


def main():
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)

        # ── 1. 只有头部、零像素：旧代码在这里返回 (1,1,b"")
        header_only = root / "hdr"
        header_only.mkdir()
        (header_only / "only-header.ppm").write_bytes(b"P6\n1 1\n255\n")
        rc, out = run_main(header_only)
        if rc == 0:
            bad("只有头部的 PPM 让 ppm2png 退出 0（凭空写出一张图）")
        elif (header_only / "only-header.png").exists():
            bad("只有头部的 PPM 仍然写出了 PNG")
        else:
            good(f"只有头部、零像素 → rc={rc}，没有写出 PNG")
        expect_raises("只有头部、零像素（函数级）", b"P6\n1 1\n255\n", "旧代码返回 (1,1,b'')")

        # ── 2. 头部字段在 EOF 处被截断：旧代码在这里死循环
        expect_raises("头部字段截断", b"P6\n80", "内层 while 在 EOF 死循环")
        expect_raises("空文件", b"", "连 P6 都不是")
        expect_raises("非 P6", b"P3\n1 1\n255\n0 0 0\n", "P3 是 ASCII 格式")
        expect_raises("像素数据截断", b"P6\n2 2\n255\n" + b"\x01" * 5, "12 字节只给了 5")
        expect_raises("像素数据多出", b"P6\n2 2\n255\n" + b"\x01" * 13, "多一个字节也不放过")
        expect_raises("maxval=65535", b"P6\n1 1\n65535\n" + b"\x00" * 6, "16 位样本会被当 8 位读")
        expect_raises("尺寸为 0", b"P6\n0 1\n255\n", "0 宽")
        expect_raises("尺寸离谱", b"P6\n99999999 1\n255\n", "超过 20000")

        # ── 3. 目录层面的假绿：空目录 / 目录不存在
        empty = root / "empty"
        empty.mkdir()
        rc, out = run_main(empty)
        if rc == 0:
            bad("空目录让 ppm2png 退出 0（与全部转换成功同码）")
        elif "没有 .ppm" not in out:
            bad(f"空目录的报错没说清原因：{out.strip()[:80]}")
        else:
            good(f"空目录 → rc={rc}：{out.strip().splitlines()[-1][:60]}")
        rc, out = run_main(root / "nope")
        if rc == 0:
            bad("不存在的目录让 ppm2png 退出 0")
        else:
            good(f"目录不存在 → rc={rc}")

        # ── 4. 正向对照：真样本必须真的转出 PNG，且宽高对得上
        mixed = root / "mixed"
        mixed.mkdir()
        w, h = 3, 2
        pix = bytes([(i * 7) % 256 for i in range(w * h * 3)])
        (mixed / "good.ppm").write_bytes(b"P6\n%d %d\n255\n" % (w, h) + pix)
        rc, out = run_main(mixed)
        png = mixed / "good.png"
        if rc != 0:
            bad(f"只有好样本时仍退出 {rc}：{out.strip()[:120]}")
        elif not png.exists():
            bad("好样本没有产出 PNG")
        else:
            blob = png.read_bytes()
            if blob[:8] != b"\x89PNG\r\n\x1a\n":
                bad("产物不是 PNG")
            else:
                iw, ih = struct.unpack(">II", blob[16:24])
                if (iw, ih) != (w, h):
                    bad(f"PNG 宽高不对：{iw}x{ih}，应为 {w}x{h}")
                else:
                    good(f"3x2 真样本 → PNG {iw}x{ih}，rc=0")

        # ── 5. 好坏混合：坏样本必须让整步失败，但好样本仍然转出来（不掩盖、不放弃）
        mixed2 = root / "mixed2"
        mixed2.mkdir()
        (mixed2 / "good.ppm").write_bytes(b"P6\n1 1\n255\n\x00\x00\x00")
        (mixed2 / "bad.ppm").write_bytes(b"P6\n1 1\n255\n")
        rc, out = run_main(mixed2)
        if rc == 0:
            bad("混合目录（含坏样本）退出 0")
        elif not (mixed2 / "good.png").exists():
            bad("混合目录里好样本没能转出来（不应该因为一个坏样本全放弃）")
        else:
            good(f"混合目录 → rc={rc}，坏样本被点名：{out.strip().splitlines()[-1][:70]}")

        # ── 6. 合法注释与多空格头部仍然要能读（别把解析改得过严）
        ok = root / "comments"
        ok.mkdir()
        (ok / "c.ppm").write_bytes(b"P6\n# created by preview\n  2  1\n255\n" + b"\x11" * 6)
        try:
            cw, ch, crgb = ppm2png.read_ppm(ok / "c.ppm")
            if (cw, ch, len(crgb)) != (2, 1, 6):
                bad(f"带注释头部解析错：{cw}x{ch}/{len(crgb)} 字节")
            else:
                good("带注释、多空格的合法头部仍能解析（2x1，6 字节）")
        except ppm2png.PpmError as exc:
            bad(f"合法注释头部被拒：{exc}")

    print("PASS: 坏 PPM/空目录一律失败，真样本照旧转换（没有静默通过）"
          if not fail else "FAIL: ppm2png 回归未通过（见上面 ✗）")
    return fail


if __name__ == "__main__":
    raise SystemExit(main())
