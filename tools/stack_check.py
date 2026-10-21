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
    "probe_task": (
        8 * 1024, 0,
        "TLS 自检任务（fnos_pair.c：8 KB，PSRAM；同步接口由调用方承担同等栈需求）",
    ),
    "cli_task": (
        4 * 1024, 0,
        "串口命令任务（fnos_wifi_cli.c：xTaskCreate 4 KB；TLS 探针仅异步排队）",
    ),
    "ui_retry_task": (
        16 * 1024, 0,
        "降级恢复任务（main.cpp：拿不到显示锁时 xTaskCreate(ui_retry_task, 16 KB)）："
        "按 5/10/20/40/60 秒退避重试 ui_build_locked() → fnos_ui_create()，跟 app_main "
        "是同一条建 UI 路径，所以它必须和 app_main 一样被算进来（审计 M11）。",
    ),
}

# 这些函数是通过函数指针被调用的，静态图看不到调用者，单独算它们的子树深度，
# 作为对应入口的 indirect 项的上限参考。
# diag_timer_cb 只在 CONFIG_FNOS_SOAK_DIAG=y 的构建里存在（72h 长测心跳，跑在 LVGL
# 任务里）；默认构建里它不在 ELF 里，worst() 会直接跳过，不影响结论。
CALLBACK_TARGETS = ["ui_tick", "pair_btn_cb", "pair_pad_cb", "pair_ok_cb",
                    "pair_cancel_cb", "pair_forget_cb", "diagnostics_cb",
                    "temp_toggle_cb", "wifi_result_cb", "wifi_connect_cb",
                    "number_event", "number_step", "number_destroy",
                    "trend_curve_task", "trend_cursors",
                    "diag_timer_cb", "motion_read_tick"]


BRANCH_OPS = {"beq", "bne", "blt", "bge", "bltu", "bgeu", "beqz", "bnez",
              "blez", "bgez", "bltz", "bgtz", "bgt", "ble", "bgtu", "bleu"}


_SIMPLE_ITANIUM = re.compile(r"^_Z(?:L)?(\d+)(.+)$")
_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def add_cpp_aliases(frames, calls):
    """给"静态 C++ 回调"补一个可查的别名，否则它们在预算里等于不存在。

    main.cpp 的诊断心跳是 `static void diag_timer_cb(...)`，符号名是 Itanium 形式的
    `_ZL13diag_timer_cbP11_lv_timer_t`；按 `diag_timer_cb` 去查会查不到，于是
    `worst()` 返回 None、静默跳过——"已纳入间接调用预算"就成了空话（C 写的事件回调
    没有这个问题，所以以前从没暴露）。Itanium 的名字带长度前缀：只取前 N 个字符当
    名字（后面是参数类型编码），嵌套名（_ZN…）与其它形式不猜，宁可查不到也不认错。
    """
    alias = {}
    for name in list(frames) + list(calls):
        m = _SIMPLE_ITANIUM.match(name)
        if not m:
            continue
        n, rest = int(m.group(1)), m.group(2)
        if n < 1 or len(rest) < n:
            continue
        base = rest[:n]
        if not _IDENT.match(base):
            continue
        if base not in frames and base not in calls:
            alias.setdefault(name, base)
    for mangled, base in alias.items():
        frames[base] = frames.get(mangled, 0)
        calls[base] = set(calls.get(mangled, ()))
    return alias


def constant_inputs(code):
    """有限常量集合的数据流：在分支汇合处取并集，未知路径不能被已知值覆盖。

    仅为寄存器栈调整求常量，不把指针/内存或被调用函数的返回值当常量。
    集合扩张超限或分析不收敛时退为未知，绝不据此放行。
    """
    if not code:
        return {}
    addresses = {addr: i for i, (addr, _, _) in enumerate(code)}
    states = {0: {"zero": frozenset({0})}}
    pending = collections.deque([0])
    steps = 0
    def signed(value):
        value &= 0xffffffff
        return value - 0x100000000 if value & 0x80000000 else value
    def number(text):
        return int(text, 16 if text.lower().lstrip("-").startswith("0x") else 10)
    stores = {"sb", "sh", "sw", "sd", "fsw", "fsd", "fence", "nop"}
    controls = {"j", "jr", "ret", "jal", "jalr", "call", "tail"}
    caller_saved = {"ra", *[f"a{i}" for i in range(8)], *[f"t{i}" for i in range(7)]}
    while pending:
        steps += 1
        if steps > len(code) * 100:
            return {}  # 有界分析不收敛，所有地址均未知。
        i = pending.popleft()
        addr, op, args = code[i]
        state = dict(states[i])
        value = None
        try:
            if op == "li":
                value = {signed(number(args[1]))}
            elif op == "lui":
                value = {signed(number(args[1]) << 12)}
            elif op == "mv":
                value = state.get(args[1])
            elif op in {"addi", "andi", "ori", "slli"} and args[1] in state:
                imm = number(args[2])
                fn = {"addi": lambda v: v + imm, "andi": lambda v: v & imm,
                      "ori": lambda v: v | imm, "slli": lambda v: v << (imm & 31)}[op]
                value = {signed(fn(v)) for v in state[args[1]]}
            elif op in {"add", "sub"} and args[1] in state and args[2] in state:
                value = {signed(x + y if op == "add" else x - y)
                         for x in state[args[1]] for y in state[args[2]]}
        except (ValueError, IndexError):
            pass
        branch = op in BRANCH_OPS
        if op in {"jal", "jalr", "call"}:
            for reg in caller_saved:
                state.pop(reg, None)
        elif not branch and op not in controls | stores:
            state.pop(args[0], None)
            if value is not None and len(value) <= 16 and args[0] != "sp":
                state[args[0]] = frozenset(value)
        state["zero"] = frozenset({0})
        successors = []
        if op not in {"j", "jr", "ret", "tail"} and i + 1 < len(code):
            successors.append(i + 1)
        if branch or op == "j":
            try:
                target = int(args[-1].split()[0], 16)
                if target in addresses:
                    successors.append(addresses[target])
            except ValueError:
                pass
        for nxt in successors:
            if nxt not in states:
                merged = state
            else:
                old = states[nxt]
                merged = {reg: old[reg] | state[reg] for reg in old.keys() & state.keys()
                          if len(old[reg] | state[reg]) <= 16}
                if merged == old:
                    continue
            states[nxt] = dict(merged)
            pending.append(nxt)
    return {code[i][0]: state for i, state in states.items()}


def load(elf, objdump):
    result = subprocess.run([objdump, "-d", "--no-show-raw-insn", elf],
                            capture_output=True, text=True)
    if result.returncode or not result.stdout.strip():
        raise ValueError("objdump 无法读取 ELF，未执行栈预算检查")
    out = result.stdout
    functions = collections.defaultdict(list)
    function = None
    for line in out.splitlines():
        header = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if header:
            function = header.group(1)
        insn = re.match(r"\s*([0-9a-f]+):\s+(\S+)\s*(.*)", line)
        if function and insn:
            addr, op, operands = insn.groups()
            args = [arg.strip() for arg in operands.split("#", 1)[0].split(",")]
            functions[function].append((int(addr, 16), op, args))
    inputs = {name: constant_inputs(code) for name, code in functions.items()}
    frames, calls = collections.defaultdict(int), collections.defaultdict(set)
    cur = None
    unresolved = set()
    depth = 0
    stack_pointers = {}
    prologue = True
    for line in out.splitlines():
        m = re.match(r"^([0-9a-f]+) <([^>]+)>:", line)
        if m:
            cur = m.group(2)
            frames[cur] = 0
            depth = 0
            stack_pointers = {}
            prologue = True
            continue
        if cur is None:
            continue
        instruction = re.match(r"\s*[0-9a-f]+:\s+(\S+)\s*(.*)", line)
        if not instruction:
            continue
        op, operands = instruction.groups()
        args = [arg.strip() for arg in operands.split("#", 1)[0].split(",")]
        def immediate(value):
            return int(value, 16 if value.lower().lstrip("-").startswith("0x") else 10)
        address = int(line.strip().split(":", 1)[0], 16)
        controls = op in BRANCH_OPS or op in {"j", "jr", "ret", "jal", "jalr", "call", "tail"}
        if controls:
            prologue = False
        # 只认入口直线段建立的帧指针；后续写该寄存器即失效。
        if not controls and op not in {"sb", "sh", "sw", "sd", "fsw", "fsd", "fence", "nop"}:
            stack_pointers.pop(args[0], None)
        if prologue and op == "addi" and len(args) == 3 and args[1] == "sp" and args[0] == "s0":
            try:
                stack_pointers[args[0]] = -depth + immediate(args[2])
            except ValueError:
                pass
        if args[0] == "sp" and op in {"addi", "add", "sub"} and len(args) == 3:
            delta = None
            if op == "addi":
                try:
                    delta = immediate(args[2])
                    if args[1] != "sp":
                        delta += stack_pointers[args[1]] + depth
                except (KeyError, ValueError):
                    delta = None
            elif args[1] == "sp":
                values = inputs.get(cur, {}).get(address, {}).get(args[2])
                if values:
                    # 多条可达路径的常量取更大的向下调整，避免静默低估。
                    delta = min(-v if op == "sub" else v for v in values)
            if delta is None:
                unresolved.add(cur)
                delta = 0
            depth = max(0, depth - delta)
            frames[cur] = max(frames[cur], depth)
        elif args[0] == "sp" and not controls and op not in {"sb", "sh", "sw", "sd", "fsw", "fsd", "fence", "nop"}:
            if op == "mv" and len(args) == 2 and args[1] in stack_pointers:
                depth = max(0, -stack_pointers[args[1]])
            else:
                unresolved.add(cur)
        if op in {"ret", "jr", "tail"}:
            depth = 0
        m = re.search(r"\b(jal|call)\s+[0-9a-f]+ <([^>+]+)", line)
        if m and m.group(2) != cur:
            calls[cur].add(m.group(2))
    for name in unresolved:
        frames[name] = -1  # 未知动态帧；不能作为零帧给检查入口放行。
    add_cpp_aliases(frames, calls)
    return frames, calls


def worst(frames, calls, root):
    """从 root 出发、单条调用链（不走环）上栈帧之和最大的路径。"""
    best = (0, [])
    def dfs(node, path, total, seen):
        nonlocal best
        if frames.get(node, 0) < 0:
            raise ValueError(f"{node}: 调用树含无法静态解析的栈调整")
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
    try:
        frames, calls = load(a.elf, a.objdump)
    except (OSError, ValueError) as e:
        print(f"栈检查失败：{e}", file=sys.stderr)
        return 2
    missing = [root for root in ROOTS if root not in frames]
    if missing:
        print("栈检查失败：缺少任务入口 " + ", ".join(missing), file=sys.stderr)
        return 2
    try:
        for entry in list(ROOTS) + CALLBACK_TARGETS:
            worst(frames, calls, entry)
    except ValueError as error:
        print(f"栈检查未判定：{error}", file=sys.stderr)
        return 2
    print(f"{os.path.relpath(a.elf, REPO)}：{len(frames)} 个函数\n")
    unknown = sum(value < 0 for value in frames.values())
    if unknown:
        print(f"另有 {unknown} 个函数含未知栈调整，未在上述任务/显式回调的静态调用树内；不属于本门已测范围。\n")

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
