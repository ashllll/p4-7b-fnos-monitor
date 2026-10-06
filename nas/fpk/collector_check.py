#!/usr/bin/env python3
"""截断真的发生了吗？——六个段各喂 50 条，看它是不是老老实实按上限裁。

为什么不能靠别处的检查：`contract_check.py` 比的是 `DEFAULT_LIMITS` 这个**常量**
和板子的数组容量——它保证"两边数字一样"，但**保证不了代码真的照着这个数字裁**。
套件里那条 `trunc.limits` 断言查的是**声明**，也一样。证伪时把 `vols` 的截断去掉，
两个检查**都是绿的**：声明还写着 12，实际却有多少发多少。

而这类"声明与实际不符"在本机是**测不出来**的：macOS 上只有一两个卷、零个阵列、
零个温度传感器——数量本来就低于上限，裁不裁看起来一模一样（同一个空断言陷阱，
这个项目里已经踩过四次）。

所以这里绕开真实数据：直接给 `_trim()` 喂 50 条合成数据。
它快、确定、而且**不可能因为"数据不够多"而变成空断言**。

用法：python3 nas/fpk/collector_check.py
退出码非 0 表示某个段没按上限裁，或者总数记错了。
"""

import importlib.util
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COLLECTOR = os.path.join(HERE, "nasscreencompanion", "app", "server", "fnos_collector.py")
SECTIONS = ("docker", "disks", "alerts", "vols", "raid", "temps")


def main():
    if not os.path.exists(COLLECTOR):
        print(f"找不到 {COLLECTOR}")
        return 2
    sys.dont_write_bytecode = True          # 别在包目录里留 __pycache__
    spec = importlib.util.spec_from_file_location("nsc_collector", COLLECTOR)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)

    obj = m.Collector.__new__(m.Collector)   # 只借 _trim 与 _totals，不走 __init__
    obj._totals = {}

    bad = 0
    print(f"{'段':10} {'上限':>4} {'喂进去':>6} {'裁完':>5} {'记的总数':>7}")
    for name in SECTIONS:
        lim = m.DEFAULT_LIMITS.get(name)
        if lim is None:
            print(f"  ✗ {name} 不在 DEFAULT_LIMITS 里 —— 那就没人管它裁不裁")
            bad += 1
            continue
        got = obj._trim(name, list(range(50)))
        total = obj._totals.get(name)
        ok = (len(got) == lim and total == 50)
        print(f"{'✓' if ok else '✗'} {name:9} {lim:>4} {50:>6} {len(got):>5} {str(total):>7}")
        if not ok:
            bad += 1
            if len(got) != lim:
                print(f"      裁成了 {len(got)} 条，应当 {lim} 条 —— 声明和实际对不上")
            if total != 50:
                print(f"      记的总数是 {total}，应当 50 —— trunc.totals 会报错数")

    # 上限之内不许动它
    for name in SECTIONS:
        lim = m.DEFAULT_LIMITS[name]
        if lim > 3:
            got = obj._trim(name, [1, 2, 3])
            if len(got) != 3:
                print(f"  ✗ {name}：只喂 3 条（上限 {lim}）却被裁成了 {len(got)} 条")
                bad += 1

    # `_trim()` 自己是对的，不等于**六个段都用了它**——证伪时把 vols 的调用点改回
    # `return vols`，上面那张表照样全绿（因为检查直接调的 `_trim`）。
    # 所以再静态核一遍调用点：每个段都必须真的走 `_trim`。
    src = io.open(COLLECTOR, encoding="utf-8").read()
    for name in SECTIONS:
        if f'self._trim("{name}"' not in src:
            print(f"  ✗ {name} 没有走 _trim() —— 上限声明得再漂亮也不会被执行")
            bad += 1
    # 顺手确认没有漏网的硬编码截断（`rates[:10]` 那种）
    import re as _re
    hard = _re.findall(r"return \w+\[:(\d+)\]", src)
    if hard:
        print(f"  ✗ 还有硬编码的截断 {hard} —— 这类写法不读 DEFAULT_LIMITS，"
              f"改了上限它不会跟着变")
        bad += 1

    if bad:
        print(f"\n{bad} 处不对。截断不生效时，板子那边是**静默少数据**——"
              f"界面上就是「只有这么多」，用户不会知道还有几条没显示。")
        return 1
    print("\n六段都按 DEFAULT_LIMITS 裁、且总数记的是截断前的值 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
