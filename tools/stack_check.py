#!/usr/bin/env python3
"""静态栈深度检查：从关键任务入口出发，算调用链上栈帧之和的**下界**。

为什么需要它：这个项目被同一个坑烧过两次——`fnos_ui_create()` 这种"一次性建一大堆
LVGL 构件"的函数，栈帧能到 11 KB 量级，而主任务栈默认只有 3584 字节（已调到 16384）。
症状是 `Guru Meditation Error: Core 0 panic'ed (Stack protection fault)`，崩溃 PC
落在堆分配里（看起来像内存问题），设备每隔几秒重启一次。**主机预览永远测不出来**
（主机栈是 MB 级），只有真机上电才暴露——所以要在烧录之前先静态量一遍。

它是**下界**，不是精确值：
  - 只跟静态 `jal/call` 边，**函数指针调用（LVGL 事件回调、timer 回调）看不到**，
    所以对那些入口要显式加一项 `indirect`（见下表），那部分靠人工给上限；
  - 编译器内联、中断嵌套、`printf` 浮点路径的实际行为都可能比静态估算更宽或更窄。

用法：python3 tools/stack_check.py [--elf build/fnos_monitor.elf] [--verbose]
改动 UI 结构、往启动路径上加构件之后跑一次；退出码非 0 表示有入口超出预算。
"""

import argparse
import collections
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 入口 → (栈预算字节, 额外的人工上限字节, 说明)
# 预算来自 main/main.cpp 与 sdkconfig，改了那里要同步改这里。
ROOTS = {
    "app_main": (
        16384, 0,
        "主任务：app_main → fnos_ui_create()（一次性建全部页面）→ fnos_data_start()。"
        "CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384",
    ),
    "lvgl_worker": (
        12 * 1024, None,          # None = 用 CALLBACK_TARGETS 里量出来的最大值
        "LVGL 任务（main.cpp 里 task_stack_size = 12 KB）：lvgl_worker → lv_timer_handler "
        "→ 【函数指针】ui_tick / 各 *_cb 事件回调。indirect 自动取这些回调子树的最大值。",
    ),
    "poll_task": (
        6 * 1024, 0,
        "数据轮询任务（fnos_data.c：xTaskCreatePinnedToCoreWithCaps 6 KB，栈在 PSRAM）",
    ),
    "pair_task": (
        7 * 1024, 0,
        "配对任务（fnos_pair.c：7 KB，栈在 PSRAM）",
    ),
}

# 这些函数是通过函数指针被调用的，静态图看不到调用者，单独算它们的子树深度，
# 作为对应入口的 indirect 项的上限参考。
CALLBACK_TARGETS = ["ui_tick", "pair_btn_cb", "pair_pad_cb", "pair_ok_cb",
                    "pair_cancel_cb", "pair_forget_cb", "diagnostics_cb"]


def load(elf, objdump):
    out = subprocess.run([objdump, "-d", "--no-show-raw-insn", elf],
                         capture_output=True, text=True).stdout
    frames, calls = collections.defaultdict(int), collections.defaultdict(set)
    cur = None
    for line in out.splitlines():
        m = re.match(r"^([0-9a-f]+) <([^>]+)>:", line)
        if m:
            cur = m.group(2)
            continue
        if cur is None:
            continue
        m = re.search(r"addi\s+sp,sp,-(\d+)", line)
        if m:
            frames[cur] = max(frames[cur], int(m.group(1)))
        m = re.search(r"\b(jal|call)\s+[0-9a-f]+ <([^>+]+)", line)
        if m and m.group(2) != cur:
            calls[cur].add(m.group(2))
    return frames, calls


def worst(frames, calls, root):
    """从 root 出发、单条调用链（不走环）上栈帧之和最大的路径。"""
    best = (0, [])
    def dfs(node, path, total, seen):
        nonlocal best
        if total > best[0]:
            best = (total, list(path))
        for c in calls.get(node, ()):
            if c in seen:
                continue
            seen.add(c)
            path.append(c)
            dfs(c, path, total + frames.get(c, 0), seen)
            path.pop()
            seen.discard(c)
    if root not in frames and root not in calls:
        return None
    dfs(root, [root], frames.get(root, 0), {root})
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default=os.path.join(REPO, "build", "fnos_monitor.elf"))
    ap.add_argument("--objdump", default="riscv32-esp-elf-objdump")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(a.elf):
        print(f"找不到 {a.elf}，先 ./idf.sh build（注意要 export DEVELOPER_DIR）")
        return 2
    frames, calls = load(a.elf, a.objdump)
    print(f"{os.path.relpath(a.elf, REPO)}：{len(frames)} 个函数\n")

    cb_depth = {}
    for t in CALLBACK_TARGETS:
        r = worst(frames, calls, t)
        if r:
            cb_depth[t] = r[0]
    if cb_depth:
        top = sorted(cb_depth.items(), key=lambda kv: -kv[1])[:5]
        print("函数指针回调的子树深度（静态）:")
        for n, v in top:
            print(f"  {v:6d} B  {n}")
        print()

    over = 0
    auto_indirect = max(cb_depth.values()) if cb_depth else 0
    for root, (budget, indirect, why) in ROOTS.items():
        # indirect 写 None 的入口，用实测的回调子树最大值 —— 手填一个数字会低估，
        # 而低估正是这个检查最危险的失效方式（会给出虚假的"✓"）。
        if indirect is None:
            indirect = auto_indirect
        r = worst(frames, calls, root)
        print(f"【{root}】预算 {budget} B —— {why}")
        if r is None:
            print("  ！在 ELF 里找不到这个入口\n")
            continue
        total, path = r
        used = total + indirect
        pct = 100.0 * used / budget
        tail = " → ".join(path[-4:]) if len(path) > 4 else " → ".join(path)
        print(f"  静态最深 {total} B（{len(path)} 层：…{tail}）"
              + (f" + 函数指针上限 {indirect} B" if indirect else ""))
        verdict = "✓" if used < budget * 0.75 else ("偏紧" if used < budget else "超了")
        print(f"  合计 {used} B / {budget} B = {pct:.0f}%  {verdict}")
        if a.verbose:
            print("  完整路径：" + " → ".join(path))
        if used >= budget:
            over += 1
        print()

    if over:
        print(f"{over} 个入口超出预算：先把构件建到别的任务上，或调大对应栈预算。")
        return 1
    print("全部入口在预算内。注意这是静态下界，函数指针与中断嵌套不在内。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
