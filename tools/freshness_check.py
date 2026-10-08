#!/usr/bin/env python3
"""产物是不是比它的源码旧？——"全绿"必须是对**当前代码**说的。

为什么需要它：这个项目有三套产物、三套编译路径，而它们的验证成本差得很远——

  · `nas/fpk/nasscreencompanion.fpk`  ← `nas/fpk/nasscreencompanion/`
  · `build/fnos_monitor.bin`          ← `main/` + `components/` + `sdkconfig*`
  · `tools/preview/build/preview`     ← `components/fnos_monitor/` 的 UI 子集 + 字体

便宜的那条（改文案 → 跑预览）会被反复跑，贵的那条（`./idf.sh build`，两分钟起）
很容易被跳过。于是出现一种最坏的情况：**源码里有编不过的东西，而所有检查都是绿的**
—— 因为它们检查的是上一次编出来的那个二进制。

这不是假设。第 12 轮就撞上了：`components/fnos_monitor/fnos_ui.c` 里留了个调试函数，
少传一个参数，`./idf.sh build` 直接报 `too few arguments`；而它加进来之后固件一次都
没编过（`build/fnos_monitor.bin` 的时间戳比 `fnos_ui.c` 早 7 分钟）。当时预览也是绿的，
因为预览那一次也没重跑。

所以这里只做一件很笨但很有效的事：**比较产物与源码的最新 mtime**，把"谁比产物新"
直接列出来。它不编译、不联网、一秒出结果，适合挂在每次报告生成的最前面。

用法：python3 tools/freshness_check.py [--quiet]
退出码非 0 表示有产物过期（列出的文件就是需要重新构建的原因）。
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 源码扩展名（只在源码树里比，不碰 build/ 与 out/）
SRC_EXT = (".c", ".cpp", ".h", ".hpp", ".cxx")

ARTIFACTS = [
    {
        "name": "NAS 应用包",
        "artifact": "nas/fpk/nasscreencompanion.fpk",
        "roots": ["nas/fpk/nasscreencompanion"],
        # 包靠 fnpack 打，任何随包文件变了都得重打
        "extra": [],
        "how": "(cd nas/fpk && fnpack build --directory nasscreencompanion)",
    },
    {
        "name": "开发板固件",
        "artifact": "build/fnos_monitor.bin",
        "roots": ["main", "components"],
        "extra": ["sdkconfig", "sdkconfig.defaults", "CMakeLists.txt", "partitions.csv"],
        # 注释掉：managed_components/ 是依赖解包目录，构建过程本身会写它，比进去只会误报
        "how": "export DEVELOPER_DIR=/Library/Developer/CommandLineTools && ./idf.sh build",
    },
    {
        "name": "主机预览",
        # 只列**真的会被编进去**的东西：预览编的是 fnos_ui.c + ui_kit/* + fonts/* + tools/preview/*，
        # 不编 fnos_data.c / fnos_net.c（那两个有 stub）。整目录列会把"碰了 fnos_data.c"
        # 也报成预览过期——检查一旦开始乱叫，人就不看它了，这比漏报更糟。
        "artifact": "tools/preview/build/preview",
        "roots": ["components/fnos_monitor/ui_kit",
                  "components/fnos_monitor/fonts",
                  "tools/preview"],
        "extra": ["components/fnos_monitor/fnos_ui.c"],
        "how": "bash tools/preview/run.sh",
    },
]


def newest_sources(roots, extra):
    newest = 0.0
    files = []
    for rel in roots:
        base = os.path.join(REPO, rel)
        for dirpath, dirnames, filenames in os.walk(base):
            # 跳过构建/缓存目录，它们不是源码
            dirnames[:] = [d for d in dirnames
                           if d not in ("build", "out", "__pycache__", ".git")]
            for fn in filenames:
                if not fn.endswith(SRC_EXT) and not fn.endswith(".sh") \
                        and not fn.endswith(".js") and not fn.endswith(".html") \
                        and not fn.endswith(".py") and fn not in ("manifest", "config",
                                                                  "privilege", "resource"):
                    continue
                p = os.path.join(dirpath, fn)
                try:
                    m = os.path.getmtime(p)
                except OSError:
                    continue
                files.append((m, os.path.relpath(p, REPO)))
    for rel in extra:
        p = os.path.join(REPO, rel)
        if os.path.exists(p):
            files.append((os.path.getmtime(p), rel))
        else:
            print(f"  ! freshness_check：配置里写的 {rel} 不存在（配置过期了）")
    if files:
        newest = max(m for m, _ in files)
    return newest, files


def main():
    quiet = "--quiet" in sys.argv
    stale = 0
    rows = []
    for spec in ARTIFACTS:
        apath = os.path.join(REPO, spec["artifact"])
        if not os.path.exists(apath):
            rows.append((spec["name"], spec["artifact"], None, [], spec["how"]))
            stale += 1
            continue
        amt = os.path.getmtime(apath)
        smt, files = newest_sources(spec["roots"], spec["extra"])
        newer = sorted([(m, f) for m, f in files if m > amt], reverse=True)
        rows.append((spec["name"], spec["artifact"], amt, newer, spec["how"]))
        if newer:
            stale += 1

    for name, apath, amt, newer, how in rows:
        if amt is None:
            print(f"  ✗ {name}：产物不存在（{apath}）—— 跑：{how}")
            continue
        if not newer:
            print(f"  ✓ {name}：不比源码旧")
            continue
        print(f"  ✗ {name}：产物比源码旧，以下改动还没进产物（最多列 5 个）")
        for m, f in newer[:5]:
            print(f"      · {f}")
        if len(newer) > 5:
            print(f"      … 另有 {len(newer) - 5} 个")
        print(f"    重新构建：{how}")

    if stale:
        print(f"\n{stale} 个产物需要重新构建。产物过期时的「全绿」是对旧代码说的，不算数。")
        return 1
    if not quiet:
        print("\n三个产物都不比源码旧 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
