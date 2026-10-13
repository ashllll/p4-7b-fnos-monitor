#!/usr/bin/env python3
"""板子读的字段，NAS 必须都在 —— 跨语言协议契约检查。

为什么需要它：固件用 cJSON 按**字段名**取值，取不到就是 0/空字符串，界面上
表现为"某个指标一直是 0"，没有报错、没有日志。NAS 侧把 `load1` 改名成 `load_1`、
或者把某个段从对象改成数组，板子不会有任何反应，只会安静地显示错的东西。
这条链路上唯一的防线就是"名字两边必须是同一份清单"。

做法：不用手抄清单（手抄的清单一定会过期），而是**从固件源码里现抓**它读了哪些
字段，再逐项到 NAS 侧的产出里核对：
  - 本机跑得出数据的段（cpu/mem/net…）→ 直接对着真实响应核；
  - 本机跑不出数据的段（macOS 没有 /proc、/sys，vols/raid/disks/temps/docker/zfs 是空的）
    → 对着 `fnos_collector.py` 里构造该段的那个方法核键名。

用法：
    python3 nas/fpk/contract_check.py                    # 开发机：本地采一帧来核对
    python3 nas/fpk/contract_check.py --status s.json    # 拿真 NAS 的一帧来核对（最有用）
    python3 nas/fpk/contract_check.py --emit             # 改了固件读的字段后，重生成随包清单
    NSC_COLLECTOR=/path/to/fnos_collector.py python3 contract_check.py   # 直接在 NAS 上跑

**拿真 NAS 的一帧来核对**是推荐用法：NAS 上每个采集段都有真实数据，48 个字段会全部
走"对着真实响应核对"，不留任何靠源码猜的条目：

    TOK=<管理页配对拿到的令牌>
    curl -s --cacert server.crt -H "Authorization: Bearer $TOK" \
         https://<NAS>:8798/api/v1/status > /tmp/status.json
    python3 nas/fpk/contract_check.py --status /tmp/status.json

退出码非 0 表示有字段对不上。
"""

import ast
import importlib.util
import io
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_FIRMWARE = os.path.join(REPO, "components", "fnos_monitor", "fnos_snapshot.c")
FIELDS_JSON = os.path.join(HERE, "board_fields.json")
COLLECTOR = os.environ.get(
    "NSC_COLLECTOR",
    os.path.join(HERE, "nasscreencompanion", "app", "server", "fnos_collector.py"))

# 状态帧的最坏情况：各段都到上限时的 payload 有多大。每项的字节数是**量出来的**
# （见 docs 里那节），不是拍的：满载实测约 8.3 KB，而板子原来只有 8 KB —— 会截断，
# cJSON 解不出来，板子一直显示「NAS 返回的数据解析不了」，而 NAS 那边一切正常。
# raid 从 90 提到 130：v1.2.5 起每行多两个键 —— `"health":"degraded"` 与
# `"what":"recovery"`（板端 RAID 卡与告警分级都靠它们），最长约 34 字节。
# 放在模块级是因为 tools/parsecheck/make_payload.py 也要读它：满载样本的条数按同一份
# 每项字节数反算，免得两边各说各话（那边曾经在取不到上限时退化成 10**9，把检查挂死）。
PER_ENTRY = {"vols": 110, "raid": 130, "disks": 220, "temps": 45,
             "docker": 240, "alerts": 250}
OVERHEAD = 1500          # cpu/mem/net/mods/trunc/caps 这些固定段
# 本轮到底有没有真的比过"NAS 上限 vs 板子容量"：没有比过时最终那行判定必须说出来
# 而不是照旧 ✔（审计 M23 —— 死掉的 RX_BUF_SIZE 检查让"没核对"印成了"核对通过"）。
CAPACITY_CHECKED = False

# 列表段 → 采集端里构造它的方法名（机器核对用；改名时两边一起改）
DOCKER_TRANSPORT = os.path.join(os.path.dirname(COLLECTOR), "docker_api.py")
TELEMETRY_SNAPSHOT = os.path.join(os.path.dirname(COLLECTOR), "telemetry_snapshot.py")

LIST_METHOD = {
    "vols": "_volumes", "raid": "_raid", "disks": "_disk_io", "temps": "_temps",
    "docker": "_docker", "alerts": "_alerts", "netifs": "_net",
}
# 对象段 → 采集端里构造它的方法名（本机有数据时走真实响应，没有时退回源码核对）
OBJ_METHOD = {"cpu": "_cpu", "mem": "_mem", "net": "_net", "zfs": "_zfs"}


def read_text(path):
    with open(path,encoding="utf-8") as f:
        return f.read()


def firmware_reads(path):
    """从 fnos_data.c 里抓出 (JSON 段名, 字段名) 列表。

    两个必须处理的地方：
      - C 局部变量名不一定等于 JSON 段名（`const cJSON *raids = ...("raid")`），
        所以先扫一遍 `cJSON_GetObjectItemCaseSensitive(父, "键")` 建立映射；
      - 数组里的 `it` 要归属到最近一次 `cJSON_ArrayForEach(it, <变量>)`。
    """
    src = read_text(path)
    if "bool fnos_status_parse" in src:
        scope = {"root": "root", "net": "net", "interfaces": "netifs"}
        reads = []
        for line in src[src.index("bool fnos_status_parse"):].splitlines():
            for var, parent, key in re.findall(r'(\w+)\s*=\s*item\((\w+),"(\w+)"\)', line):
                scope[var] = key
            loop = re.search(r'cJSON_ArrayForEach\(v,array\(root,"(\w+)"\)\)', line)
            if loop:
                scope["v"] = loop.group(1)
            elif 'cJSON_ArrayForEach(v,interfaces)' in line:
                scope["v"] = "netifs"
            elif 'cJSON_ArrayForEach(v,modules)' in line:
                scope["v"] = "modules"
            # 取值函数名单要跟着 fnos_snapshot.c 走。审计 M03 把"double 直接赋值"换成
            # 了按类型钳位的 inum/unum/lnum/fnum，这里漏列就会**静默**少核 36 个字段
            # （62 → 26），清单看起来照旧，实际已经有一多半没人核了。
            for var, key in re.findall(
                    r'(?:number|string|item|inum|unum|lnum|fnum)\((\w+),"(\w+)"\)', line):
                if var in scope and var not in ("root",) and key != "interfaces":
                    reads.append((scope[var], key))
            for var, key in re.findall(r'TEXT\([^,]+,(\w+),"(\w+)"\)', line):
                if var in scope:
                    reads.append((scope[var], key))
            for key in re.findall(r'cJSON_IsTrue\(item\(root,"(\w+)"\)\)', line):
                reads.append(("root", key))
            for key in re.findall(r'(?:number|inum|unum|lnum|fnum)\(root,"(\w+)"\)', line):
                reads.append(("root", key))
        return list(dict.fromkeys(reads))
    # 变量名 → JSON 键
    varmap = {}
    for m in re.finditer(r"const cJSON \*(\w+)\s*=\s*cJSON_GetObjectItemCaseSensitive\(\s*\w+\s*,\s*\"([A-Za-z_0-9]+)\"", src):
        varmap[m.group(1)] = m.group(2)
    reads = []
    scope = None
    for line in src.splitlines():
        m = re.search(r"cJSON_ArrayForEach\(\s*\w+\s*,\s*(\w+)\s*\)", line)
        if m:
            scope = m.group(1)
            continue
        m = re.search(r"j(?:num|str|int|bool)\(\s*(\w+)\s*,\s*\"([A-Za-z_0-9]+)\"", line)
        if not m:
            continue
        var, key = m.group(1), m.group(2)
        if var == "root":
            name = "root"
        elif var == "it":
            name = varmap.get(scope, scope)
        else:
            name = varmap.get(var, var)
        if name:
            reads.append((name, key))
    seen, out = set(), []
    for item in reads:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def live_status(path=None):
    """一帧真实响应。给了 path 就用它（推荐：来自真 NAS，每个段都有数据），
    否则在本机跑一次采集器（macOS 上大部分段是空的，只能靠源码兜）。"""
    if path:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    out = subprocess.run([sys.executable, COLLECTOR, "--sample"],
                         capture_output=True, text=True, timeout=30, check=True).stdout
    sample = json.loads(out)
    if not os.path.exists(TELEMETRY_SNAPSHOT):
        return sample
    spec = importlib.util.spec_from_file_location("contract_telemetry_snapshot", TELEMETRY_SNAPSHOT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    body = module.PublishedSnapshot(sample).status_body("contract-check", "0", time.time())
    return json.loads(body)


def method_body(path, name):
    with open(path, encoding="utf-8") as f:
        src = f.read()
    tree = ast.parse(src)
    matches = [node for node in ast.walk(tree)
               if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name == name]
    if len(matches) != 1:
        return None
    return ast.get_source_segment(src, matches[0])


def load_reads(argv):
    """字段清单的来源：优先现抓固件源码（开发机上），否则读随包发出的清单。

    为什么要落一份 JSON：这条检查最有价值的地方是在**真 NAS 上**跑——那里每个
    采集段都有真实数据，"对着真实响应核对"能覆盖全部字段；可 NAS 上没有固件源码。
    所以开发机上用 --emit 生成一份清单随包带过去，两边不会各写一份。
    """
    args = [a for a in argv[1:] if not a.startswith("-")]
    if "--status" in argv:                      # --status 的值不是固件源码路径
        i = argv.index("--status")
        val = argv[i + 1] if i + 1 < len(argv) else None
        args = [a for a in args if a != val]
    if args:
        return firmware_reads(args[0]), args[0]
    if os.path.exists(DEFAULT_FIRMWARE):
        return firmware_reads(DEFAULT_FIRMWARE), DEFAULT_FIRMWARE
    with open(FIELDS_JSON, encoding="utf-8") as f:
        data = json.load(f)
    return [tuple(x) for x in data["fields"]], FIELDS_JSON + "（随包清单，源自 " + data["source"] + "）"


def all_source_keys():
    """采集端源码里出现过的所有 `"键":`，用来兜一条底：板子读的字段名，
    在 NAS 的采集或响应发布代码里至少得存在（哪怕本机这条分支走不到）。"""
    paths = (COLLECTOR, DOCKER_TRANSPORT, TELEMETRY_SNAPSHOT)
    src = "\n".join(read_text(path) for path in paths if os.path.exists(path))
    # 两种写法都算：字典字面量 `"k": v`，以及下标赋值 `snap["k"] = v`
    # （顶层快照的很多字段是后一种，只认字面量会误报）。
    keys = set(re.findall(r'["\']([A-Za-z_0-9]+)["\']\s*:', src))
    keys |= set(re.findall(r'\[\s*["\']([A-Za-z_0-9]+)["\']\s*\]\s*=', src))
    keys |= set(re.findall(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*=", src))
    return keys


def preview_size_problems():
    """主机预览的默认画布尺寸必须等于真机面板尺寸。

    从实际 lv_display_create() 参数核对默认值，兼容常量和运行时变量。
    PREVIEW_SIZE 可覆盖默认值做适配矩阵；那些样张是其他分辨率的取证，
    默认画布才与 BSP 的 BSP_LCD_H_RES/BSP_LCD_V_RES 对照。
    """
    out = []
    bsp = os.path.join(REPO, "components/esp32_p4_wifi6_touch_lcd_7b/include/bsp/display.h")
    prev = os.path.join(REPO, "tools/preview/preview.c")
    for f in (bsp, prev):
        if not os.path.exists(f):
            print(f"（跳过预览尺寸核对：{os.path.relpath(f, REPO)} 不在）")
            return []
    b = read_text(bsp)
    p = read_text(prev)

    def num(text, name):
        # The adaptive preview stores defaults in variables, not SCR_W/H macros.
        escaped = re.escape(name)
        m = re.search(r"#define\s+%s\b\s+\(?\s*(\d+)" % escaped, text)
        if not m:
            m = re.search(r"\b%s\s*=\s*\(?\s*(\d+)" % escaped, text)
        return int(m.group(1)) if m else None

    display = re.search(r"\blv_display_create\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)", p)
    if not display:
        return ["取不到 lv_display_create() 的尺寸来源，没法核预览默认画布尺寸"]
    width, height = display.groups()
    pairs = [("BSP_LCD_H_RES", num(b, "BSP_LCD_H_RES"), width, num(p, width)),
             ("BSP_LCD_V_RES", num(b, "BSP_LCD_V_RES"), height, num(p, height))]
    for bn, bv, pn, pv in pairs:
        if bv is None or pv is None:
            out.append(f"取不到 {bn} 或 {pn} 的值，没法核预览默认画布尺寸")
        elif bv != pv:
            out.append(f"面板是 {bn}={bv}，而预览默认画布 {pn}={pv}"
                       f"—— 默认样张不是真机上会看到的样子（P4 的截图就从这儿出）")
    if not out:
        print(f"预览默认画布 {pairs[0][3]}×{pairs[1][3]} 与真机面板 "
              f"{pairs[0][1]}×{pairs[1][1]} 一致")
    return out


def main():
    if "--emit" in sys.argv:
        reads = firmware_reads(DEFAULT_FIRMWARE)
        with open(FIELDS_JSON, "w", encoding="utf-8") as f:
            json.dump({
                "note": "由 nas/fpk/contract_check.py --emit 从固件源码生成，别手改；"
                        "改的是 components/fnos_monitor/fnos_snapshot.c。",
                "source": os.path.relpath(DEFAULT_FIRMWARE, REPO),
                "fields": reads,
            }, f, ensure_ascii=False, indent=1)
        print(f"写出 {FIELDS_JSON}（{len(reads)} 个字段）")
        return 0

    reads, origin = load_reads(sys.argv)
    status_path = None
    if "--status" in sys.argv:
        i = sys.argv.index("--status")
        status_path = sys.argv[i + 1] if i + 1 < len(sys.argv) else None
        if not status_path:
            print("--status 后面要跟一个 JSON 文件路径"); return 2
    if not reads or any(not isinstance(row, (tuple,list)) or len(row)!=2 or
                        not all(isinstance(v,str) and v for v in row) for row in reads):
        print("字段清单缺失或无效，拒绝空检查"); return 1
    st = live_status(status_path)
    if not isinstance(st,dict):
        print("状态响应必须为对象"); return 1
    if status_path:
        origin += f"；响应来自 {status_path}"
    src_keys = all_source_keys()
    if os.path.exists(DOCKER_TRANSPORT):
        src_keys.update(re.findall(r'["\'](\w+)["\']\s*:', read_text(DOCKER_TRANSPORT)))
    problems, checked_live, checked_src = [], 0, 0

    for scope, key in reads:
        # 「本机拿不到这段数据」的各种形态都要认：列表为空、对象为空、值是 null。
        # 它们不代表协议不对，只代表这台机器（macOS 没有 /proc、/sys）采不到，
        # 这时退回采集端源码核键名。
        seg = (st.get("net") or {}).get("interfaces") if scope == "netifs" else (st.get(scope) if scope != "root" else st)
        if scope == "root":
            ok = key in st
            where = "真实响应 status"
        elif scope == "modules" and isinstance(seg, dict) and seg:
            ok = all(isinstance(value, dict) and key in value for value in seg.values())
            where = "真实响应 status.modules.*"
        elif isinstance(seg, dict) and seg:
            ok = key in seg
            where = f"真实响应 status.{scope}"
        elif isinstance(seg, list) and seg:
            ok = all(isinstance(value, dict) and key in value for value in seg)
            where = f"真实响应 status.{scope}[0]"
        elif scope in st or (scope == "netifs" and "net" in st):
            meth = LIST_METHOD.get(scope) or OBJ_METHOD.get(scope)
            if scope == "docker":
                # 容器行的键名自 H21 起在 docker_api.row_from_container() 里拼（名字完整保留 +
                # 稳定 id），containers() 只负责取数、校验和排序。只扫 containers() 会把
                # n/up/s 三个键看成"采集端没有"——那是检查跟丢了代码，不是协议不符。
                body = "\n".join(filter(None, (method_body(DOCKER_TRANSPORT, "containers"),
                                              method_body(DOCKER_TRANSPORT, "row_from_container"),
                                              method_body(DOCKER_TRANSPORT, "_enrich_metrics")))) or None
            else:
                body = method_body(COLLECTOR, meth) if meth else None
                if scope == "disks": body = (body or "") + (method_body(COLLECTOR,"_smart_enrich") or "")
            if body is None:
                problems.append(f"{scope}.{key}：找不到构造该段的方法（{meth}）")
                continue
            ok = bool(re.search(r'["\']%s["\']\s*:' % re.escape(key), body))
            ok = ok or bool(re.search(r"\[\s*['\"]%s['\"]\s*\]\s*=" % re.escape(key), body))
            ok = ok or bool(re.search(r"\b%s\s*=" % re.escape(key), body))
            where = f"采集端源码 {meth}()（本机该段为空）"
        else:
            problems.append(f"{scope}.{key}：响应里没有这个段（status.{scope}）")
            continue

        if ok:
            if "源码" in where:
                checked_src += 1
            else:
                checked_live += 1
        else:
            problems.append(f"{scope}.{key} 对不上 —— 核对处：{where}")

    # 兜底：字段名至少得在采集端代码里存在过。
    for scope, key in reads:
        if key not in src_keys:
            problems.append(f"{scope}.{key}：采集端源码里根本没有这个键名")

    print(f"清单来源：{origin}")
    print(f"固件读了 {len(reads)} 个字段；"
          f"对着真实响应核对 {checked_live} 个，对着采集端源码核对 {checked_src} 个")
    if checked_src:
        print("（有条目是靠源码核对的，说明本机采不到那些段——真 NAS 上这一项会全部走真实响应）")
    if problems:
        print("发现对不上的字段：")
        for p in problems:
            print("  ✗", p)
        return 1

    problems += limits_problems()
    problems += preview_size_problems()
    if problems:
        print("容量上限对不上：")
        for p in problems:
            print("  ✗", p)
        return 1
    print("判定：板子读的每个字段，NAS 侧都在；"
          + ("已核对可解析的静态预算估算；不证明任意动态清单容量" if CAPACITY_CHECKED
             else "未核对容量，只有字段对齐是核对过的"))
    return 0


def limits_problems():
    """板子每段能装多少条 >= NAS 截断后发多少条。

    这一类不一致最阴：NAS 按 DEFAULT_LIMITS 截断，板子装不下的部分**静默丢掉**
    ——没有错误、没有提示，界面上就是"只有这么多"。容器一多的机器最容易撞
    （NAS 发 16 个，板子的数组只有 12 个，用户永远看不到那 4 个，也不会知道有）。
    两边都是我们自己的代码，所以直接把两个数对起来。
    """
    # FNOS_MAX_* 定义在 fnos_data.h 里，不在 .c 里（DEFAULT_FIRMWARE 指的是 .c）
    limits_h = os.path.join(os.path.dirname(DEFAULT_FIRMWARE), "fnos_data.h")
    if not os.path.exists(limits_h):
        print(f"（跳过容量上限核对：找不到 {limits_h}）")
        return ["缺少固件容量头文件，容量未核对"]
    fw = read_text(limits_h)
    # HIST_BUF_SIZE 定义在 .c 里（FNOS_MAX_* 在 .h 里），两个都要读
    fw2 = fw + read_text(os.path.join(os.path.dirname(DEFAULT_FIRMWARE), "fnos_data.c"))
    cap = {m.group(1): int(m.group(2))
           for m in re.finditer(r"#define\s+FNOS_MAX_(\w+)\s+(\d+)", fw)}
    nas_path = os.path.join(os.path.dirname(DEFAULT_FIRMWARE), "..", "..",
                            "nas", "fpk", "nasscreencompanion", "app", "server",
                            "fnos_collector.py")
    nas_path = os.path.normpath(nas_path)
    if not os.path.exists(nas_path):
        print(f"（跳过容量上限核对：固件上限 {len(cap)} 个，采集端 {os.path.exists(nas_path)}）")
        return ["缺少采集端源码，容量未核对"]
    coll = read_text(nas_path)
    m = re.search(r"DEFAULT_LIMITS\s*=\s*\{([^}]*)\}", coll)
    limits = {}
    if m:
        for k, v in re.findall(r'"(\w+)"\s*:\s*(\d+)', m.group(1)):
            limits[k] = int(v)
    out = []
    if not cap:
        print("设备清单：动态快照与按需接收，数量不由 FNOS_MAX 常量截断")
    global CAPACITY_CHECKED
    CAPACITY_CHECKED = False
    for seg, lim in sorted(limits.items()):
        c = cap.get(seg.upper())
        if c is None:
            continue
        CAPACITY_CHECKED = True
        if c < lim:
            out.append(f"NAS 的 {seg} 一次最多发 {lim} 条，板子的 FNOS_MAX_{seg.upper()} 只有 {c} "
                       f"—— 多出来的会被静默丢掉（界面不会说，用户只能看到少几条）")
    if limits:
        print(f"容量上限：NAS 截断 {limits}，板子容量 "
              f"{ {k: v for k, v in cap.items() if k.lower() in limits} }")
    else:
        # 采集端已经不按常量截断了（动态快照），所以这一轮**没有**任何"NAS 上限 vs 板子容量"
        # 的数字可比。说清楚，别让最终那行 ✓ 把它盖过去（审计 M23）。
        print("容量上限：采集端已改成动态快照，NAS 侧没有截断常量可比（板子内部自洽见下一行）")

    # 板子**自己**的两个数字也得对得上：它一次性能收多少行历史，与它内部环形缓冲
    # 的格数、以及接收缓冲的字节数。板子现在会带 `?n=FNOS_HIST_MAX` 去要，所以
    # 接收缓冲必须装得下这么多行（按每行 64 字节估，另留 512 字节给 JSON 外壳）。
    m = re.search(r"#define\s+HIST_BUF_SIZE\s+\(?\s*(\d+)\s*\*\s*(\d+)\s*\)?", fw2)
    m2 = re.search(r"#define\s+HIST_BUF_SIZE\s+(\d+)", fw2)
    if m:
        buf = int(m.group(1)) * int(m.group(2))
    elif m2:
        buf = int(m2.group(1))
    else:
        buf = 0
    # 注意名字是 `FNOS_HIST_MAX`（不是 `FNOS_MAX_HIST`）——上面那个通用正则抓的是
    # `FNOS_MAX_*` 那一族，抓不到它，得单独取
    mh = re.search(r"#define\s+FNOS_HIST_MAX\s+(\d+)", fw2)
    hist = int(mh.group(1)) if mh else None
    # 状态帧的最坏情况：各段都到上限时的 payload 有多大（常量在模块级，见文件头）
    if limits:
        worst = OVERHEAD + sum(PER_ENTRY.get(k, 0) * v for k, v in limits.items())
        # 状态帧的接收预算是 Kconfig 的 CONFIG_FNOS_STATUS_MAX_BYTES（默认 262144）。
        # 这里以前找的是 `RX_BUF_SIZE` —— 那个宏早就不存在了（审计 M23）：正则抓不到、
        # rx 恒为 0，于是整段判断被跳过，而 main() 照样打印"容量上限也不丢数据 ✓"，
        # 等于把"没核对"报成了"核对通过"。
        kcfg = os.path.join(os.path.dirname(DEFAULT_FIRMWARE), "Kconfig")
        budget = None
        if os.path.exists(kcfg):
            mk = re.search(r"config\s+FNOS_STATUS_MAX_BYTES\b.*?default\s+(\d+)",
                           read_text(kcfg), re.S)
            if mk:
                budget = int(mk.group(1))
        sdk = os.path.normpath(os.path.join(os.path.dirname(DEFAULT_FIRMWARE), "..", "..",
                                            "sdkconfig.defaults"))
        if os.path.exists(sdk):
            ms = re.search(r"^CONFIG_FNOS_STATUS_MAX_BYTES\s*=\s*(\d+)",
                           read_text(sdk), re.M)
            if ms:
                budget = int(ms.group(1))       # sdkconfig 覆盖 Kconfig 默认值
        if budget is None:
            out.append("找不到 CONFIG_FNOS_STATUS_MAX_BYTES 的取值（Kconfig 默认值与 "
                       "sdkconfig.defaults 都没有）—— 状态帧接收预算这一项没有核对")
        else:
            CAPACITY_CHECKED = True
            if budget < worst:
                out.append(f"CONFIG_FNOS_STATUS_MAX_BYTES={budget} 装不下满载的状态帧"
                           f"（按声明的上限估 {worst} 字节）—— 会被截断，"
                           f"板子会一直显示「NAS 返回的数据解析不了」")
            else:
                print(f"状态帧预算：CONFIG_FNOS_STATUS_MAX_BYTES={budget} ≥ 满载估计 {worst}")
    if buf and hist:
        need = hist * 64 + 512
        CAPACITY_CHECKED = True
        if buf < need:
            out.append(f"HIST_BUF_SIZE={buf} 装不下 FNOS_HIST_MAX={hist} 行"
                       f"（按每行 64 字节估，至少要 {need}）—— 多出来的会被截断，"
                       f"JSON 解不出来，板子会每秒重试一次、曲线回填永远不成功")
        else:
            print(f"板子历史预算估算：HIST_BUF_SIZE={buf} ≥ FNOS_HIST_MAX={hist}×64+512={need}（不是任意数值长度上界）")
    return out


if __name__ == "__main__":
    try:
        result = main()
    except (OSError, ValueError, TypeError, KeyError, IndexError, subprocess.SubprocessError) as e:
        print(f"契约检查未完成：{type(e).__name__}")
        result = 1
    sys.exit(result)
