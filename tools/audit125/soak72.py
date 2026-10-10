#!/usr/bin/env python3
"""72 小时实机长测：只读抓串口 → 只存数值记录 → 离线按固定判据评价 → 三态结论。

审计报告要求"72 小时长测"可复核，而长测只有一次机会（跑完三天才发现判据写错，代价是三天），
所以这里刻意把三件事分开：

1. **采集只落数值**。串口上绝大多数字节是启动日志/文本，抄进证据只会让证据里混进主机名、
   SSID、路径和令牌；真正要复核的是固件每 10s 打的那一行机器可读 JSON 里的数字。因此
   capture 只写 metrics.jsonl（{"t":<接收时刻>,"rec":<诊断对象>}），其它文本立即丢弃并只计数。
2. **不自动重开串口**。掉线时"静默重连"会把一段空白补成连续曲线，看起来像"一直在跑"；
   这里掉线就记一条 error 并收工，让 analyze 判 INCOMPLETE——宁可说"证据不全"。
3. **判据与采集分离**。判据只在 analyze 里实现，阈值是模块级常量，测试直接引用同一批常量，
   改判据会立刻在 test_soak72.py 上现形。串口静默（板子不打了但线还插着）不是掉线，
   由 analyze 的心跳覆盖率判定，这里不另设超时。

analyze 的退出码只有四种含义：0=PASS、1=FAIL、2=INCOMPLETE、3=INPUT_ERROR；**任何**输入
（空文件、只有半行、时间戳乱序、主机时钟回拨）都会落一份 summary.json 并给这四个码之一，
不会抛未捕获异常——下游只看 summary.json 就能知道"这次有没有结论、为什么"。
"文件最后一行残缺"是唯一被容忍的破损（采集被强杀时正在写的那行），其余破损一律退 3。

用法：
    python3 tools/audit125/soak72.py capture --hours 72 --elf build/fnos_monitor.elf \\
        --output soak72-results [--port /dev/cu.usbmodemXXXX] [--fault-windows win.json]
    python3 tools/audit125/soak72.py analyze --input soak72-results/metrics.jsonl \\
        --hours 72 --elf build/fnos_monitor.elf --output soak72-review [--fault-windows win.json]
退出码：0=PASS 1=FAIL 2=INCOMPLETE 3=INPUT_ERROR
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import statistics
import sys
import time

# ---------------------------------------------------------------- 判据阈值
# 阈值集中在这里：test_soak72.py 直接 import 这批常量来造两侧数据，判据与测试不会各说各话。
EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_INCOMPLETE = 2
EXIT_INPUT_ERROR = 3

MARK = "FNOS_DIAG"                  # 诊断行标记（行内定位，前面可能带 IDF 日志前缀）
BAUDRATE = 115200
ENV_PORT = "FNOS_SOAK_PORT"

HEARTBEAT_NOMINAL_S = 10.0          # 板端标称打印周期（hz）
DURATION_SLACK_S = 30.0             # 板端 up 跨度允许比标称少 30s（起止边界）
COVERAGE_MIN = 0.99                 # 心跳覆盖率
RX_GAP_MAX_S = 30.0                 # 相邻串口接收样本最大间隔
HEARTBEAT_LOST_S = 60.0             # 超过这个间隔记作"心跳中断"
UI_AGE_MAX_S = 15.0                 # LVGL 存活
STACK_MIN_BYTES = 1024              # uxTaskGetStackHighWaterMark 余量（字节）
IMIN_MIN_BYTES = 32 * 1024
IMAX_MIN_BYTES = 16 * 1024
DMIN_MIN_BYTES = 8 * 1024
PFREE_MIN_BYTES = 1024 * 1024
WARMUP_S = 2 * 3600.0               # 预热期，不参与趋势
TREND_WINDOW_S = 3600.0             # 趋势首尾各取 1h 的中位数
TREND_BUCKET_S = 600.0              # 回归用的 10 分钟中位数序列
TREND_MIN_BUCKETS = 3
IFREE_LOSS_MAX = 8 * 1024
IMAX_LOSS_MAX = 8 * 1024
PFREE_LOSS_MAX = 256 * 1024
IFREE_SLOPE_MIN = -128.0            # B/h
IMAX_SLOPE_MIN = -128.0
PFREE_SLOPE_MIN = -4096.0
POLL_OK_RATE_MIN = 0.995
FRESH_RATIO_MIN = 0.995
P95_MAX_MS = 1000
IO_AGE_MAX_S = 15.0                 # 0 <= age <= 15 才算"在线且新鲜"；-1 = 从未成功
SRC_STALL_MAX_S = 30.0              # io.src 不变超过它 = 假在线
FAULT_RECOVER_S = 60.0              # 故障窗口结束后的恢复期
MIN_SAMPLE_RATIO = 0.5              # 有效样本下限 = 标称条数 * 0.5
CLOCK_STEP_MAX_S = 60.0             # 接收时间戳回退超过它 = 主机时钟被回拨，时间轴不可信
ANOMALOUS_RESET = frozenset((4, 5, 6, 7, 9, 15))   # PANIC/INT_WDT/TASK_WDT/WDT/BROWNOUT/CPU_LOCKUP

VERDICT_EXIT = {"PASS": EXIT_PASS, "FAIL": EXIT_FAIL, "INCOMPLETE": EXIT_INCOMPLETE}

# ---------------------------------------------------------------- 单行解析（纯函数，测试直接调）
# 白名单=接口契约：键名/类型不对就整条拒收（返回 None），调用方按 malformed 计数。
# 为什么用"键集合全等"而不是"取需要的键"：这是隐私边界——固件/代理一旦往里塞
# token/ssid/path 之类的键，宁可丢样本也不能让它们落进证据文件。
_REC_SCHEMA = {
    "n": int,
    "up": int,
    "hz": int,
    "fw": str,
    "rst": int,
    "ev": [str],
    "io": {"ok": int, "fail": int, "age": int, "p95": int, "src": int},
    "ui": {"age": int},
    "mem": {"ifree": int, "imin": int, "imax": int, "dfree": int, "dmin": int,
            "pfree": int, "pmin": int},
    "stk": {"lvgl": int, "poll": int, "pair": int},
}


def _match(value, spec):
    """按 spec 校验并**重建**（不返回原对象，避免调用方再挂私有键上去）。"""
    if isinstance(spec, dict):
        if type(value) is not dict or set(value) != set(spec):
            return None
        out = {}
        for key, sub in spec.items():
            got = _match(value[key], sub)
            if got is None:
                return None
            out[key] = got
        return out
    if isinstance(spec, list):
        if type(value) is not list or any(type(x) is not str for x in value):
            return None
        return list(value)
    # 标量：用 `type(x) is int/str` 而不是 isinstance——bool 是 int 的子类，
    # 而 `"rst": true` 这种记录必须被拒；浮点也一律拒收（接口是整数，单位固定）。
    if spec is int:
        return value if type(value) is int else None
    if spec is str:
        return value if type(value) is str else None
    return None


def sanitize(rec):
    """校验一条诊断记录；不合法（缺字段/类型不对/多出未知键）返回 None。"""
    return _match(rec, _REC_SCHEMA)


def parse_line(text):
    """从一行串口文本里取诊断记录：行内定位 MARK，标记之后必须是完整 JSON。

    真实形态是 `I (123456) main: FNOS_DIAG {...}`，所以不能要求行首就是标记；
    但也不做模糊匹配——标记之后解析不出 JSON 就当 malformed，由调用方计数。
    """
    idx = text.find(MARK)
    if idx < 0:
        return None
    try:
        obj = json.loads(text[idx + len(MARK):].strip())
    except ValueError:
        return None
    if type(obj) is not dict:
        return None
    return sanitize(obj)


def fresh(rec):
    """在线且新鲜：io.age 是板端自报"距最近一次成功接收的秒数"，从未成功时固件给 -1。"""
    age = rec["io"]["age"]
    return 0 <= age <= IO_AGE_MAX_S


# ---------------------------------------------------------------- 装载（一切不可用都退 3）
class InputError(Exception):
    """参数/文件/记录不可用。一律退出码 3，不做猜测性修补。"""


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def load_windows(path):
    """故障窗口文件：JSON 数组，元素 {"start_s":<秒>,"end_s":<秒>}，相对采集起点。

    窗口**只影响评价，不产生故障**；重叠或 end<=start 是配置错误（退 3），
    start_s 为负同样没有意义（相对起点不可能为负），一并拒掉。
    """
    if not path:
        return []
    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = json.load(fh)
    except (OSError, ValueError) as exc:
        raise InputError("故障窗口文件读不了或不是 JSON：%s" % type(exc).__name__)
    if type(raw) is not list:
        raise InputError("故障窗口文件顶层必须是 JSON 数组")
    wins = []
    for i, el in enumerate(raw):
        if type(el) is not dict or set(el) != {"start_s", "end_s"}:
            raise InputError("第 %d 个窗口必须是 {\"start_s\":..,\"end_s\":..}" % i)
        a, b = el["start_s"], el["end_s"]
        if type(a) not in (int, float) or type(b) not in (int, float):
            raise InputError("第 %d 个窗口的 start_s/end_s 必须是数字" % i)
        if b <= a:
            raise InputError("第 %d 个窗口 end_s <= start_s" % i)
        if a < 0:
            raise InputError("第 %d 个窗口 start_s 为负" % i)
        wins.append((float(a), float(b)))
    wins.sort()
    for (_, prev_end), (nxt_start, _) in zip(wins, wins[1:]):
        if nxt_start < prev_end:
            raise InputError("故障窗口重叠：%g 落在前一个窗口内" % nxt_start)
    return wins


def load_metrics(path):
    """读 metrics.jsonl，返回 (samples, meta)。

    容错边界只有一条：**文件最后一行残缺**是允许的——采集进程被 Ctrl-C/kill 时正在写的那一行
    本来就写不完，把它当"整份证据不可读"是错的，记 malformed 计数、让"缺结束标记"去判 INCOMPLETE。
    中间任何位置破损、或记录本身不合契约（缺键/多键/类型错）一律 InputError：
    证据文件被改过就不能装作没看见，未知键也可能夹带令牌，绝不能静默放过。
    meta 另外记下接收时间戳回退——主机时钟被 NTP 回拨会同时污染相邻间隔和趋势时间轴。
    """
    meta = {"lines": 0, "malformed_tail": 0, "out_of_order": 0, "max_back_step_s": 0.0}
    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = fh.read().split("\n")
    except OSError as exc:
        raise InputError("打不开 metrics：%s" % type(exc).__name__)
    while raw and not raw[-1].strip():
        raw.pop()                                   # 文件以 \n 收尾时 split 出的空尾巴
    samples = []
    prev_t = None
    for j, text in enumerate(raw):
        if not text.strip():
            continue
        meta["lines"] += 1
        try:
            obj = json.loads(text)
        except ValueError:
            obj = None
        if obj is None or type(obj) is not dict or set(obj) != {"t", "rec"}:
            if j == len(raw) - 1:
                meta["malformed_tail"] += 1
                break
            raise InputError("第 %d 行破损（不是 {\"t\":..,\"rec\":..} 的完整 JSON）" % (j + 1))
        t = obj["t"]
        if type(t) not in (int, float) or type(t) is bool or not math.isfinite(t):
            raise InputError("第 %d 行的 t 不是有限数字" % (j + 1))
        rec = sanitize(obj["rec"])
        if rec is None:
            raise InputError("第 %d 行的记录不符合诊断契约（缺字段/类型不对/多出未知键）" % (j + 1))
        if prev_t is not None and float(t) < prev_t:
            meta["out_of_order"] += 1
            meta["max_back_step_s"] = max(meta["max_back_step_s"], prev_t - float(t))
        prev_t = float(t)
        samples.append({"t": float(t), "rec": rec})
    # 刻意**不排序**：采集是单线程顺序 append 的，文件顺序就是接收顺序。按 t 重排会把
    # 时钟抖动变成"计数器回退"，把一次 NTP 校正误判成设备复位；顺序类判据只看文件顺序，
    # 时间轴由 evaluate 用"单调化"后的 el 表示。
    return samples, meta


def load_capture(path):
    """capture.json：缺文件返回 None（= 缺结束标记）；文件破损退 3。"""
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError) as exc:
        raise InputError("capture.json 读不了或破损：%s" % type(exc).__name__)
    if type(data) is not dict:
        raise InputError("capture.json 顶层必须是对象")
    return data


# ---------------------------------------------------------------- 评价
def _pick(rec, path):
    for key in path:
        rec = rec[key]
    return rec


def _slope(xs, ys):
    """最小二乘斜率（xs 已换算成小时）。"""
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    var = sum((x - mx) ** 2 for x in xs)
    if var <= 0:
        return 0.0
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / var


def _result(checks, notes, hours, elf_sha, sample_count, extra=None):
    """FAIL 优先于 INCOMPLETE：能证明违反的不因为"另外还缺证据"降级成没结论。"""
    failures = ["%s: %s" % (c["name"], c["detail"]) for c in checks if c["state"] == "fail"]
    missing = ["%s: %s" % (c["name"], c["detail"]) for c in checks if c["state"] == "incomplete"]
    verdict = "FAIL" if failures else ("INCOMPLETE" if missing else "PASS")
    out = {"tool": "soak72 analyze", "verdict": verdict, "hours": hours, "elf_sha256": elf_sha,
           "sample_count": sample_count, "checks": checks, "failures": failures,
           "incomplete_reasons": missing, "notes": notes}
    out.update(extra or {})
    return out


def _notes():
    return [
        "mem.d*（DMA）与 mem.i*（内部 RAM）可能覆盖同一段堆：本报告只做逐项底线判定，两者不得相加。",
        "故障窗口只豁免「正常联网」类判据（net_health / source_progress）：窗口内以及窗口结束后 "
        "%ds 的恢复期内不评价；复位、心跳、UI、栈、堆底线在窗口内仍然严格判定。" % int(FAULT_RECOVER_S),
        "趋势区间从采集起点后 %dh 起算（预热期不参与趋势），首尾各取 %dh 的中位数，"
        "回归用 %d 分钟中位数序列。" % (int(WARMUP_S // 3600), int(TREND_WINDOW_S // 3600),
                                    int(TREND_BUCKET_S // 60)),
        "故障窗口的 start_s/end_s 是相对第一条样本接收时刻的秒数；窗口重叠或 end_s<=start_s 会退 3。",
        "在线/新鲜样本 = 0 <= io.age <= %ds；io.age = -1 表示板端「从未成功接收」，判为不在线。"
        % int(IO_AGE_MAX_S),
        "stk.* 为 0 表示该任务没起来/测不到，按 INCOMPLETE 处理，不当成「栈只剩 0 字节」。",
        "「测不到」与「测得差」分开：stk.*=0、正常窗口内 io.age/io.src 全程 -1（板端从未成功接收）"
        "属证据不可用 → INCOMPLETE；部分失败、变旧、旧帧冒充在线 → FAIL。",
    ]


def evaluate(samples, capture, hours, elf_sha, windows, load_meta=None):
    """全部判据的落点。samples=[{"t":float,"rec":dict}]，windows=[(start_s,end_s)]。"""
    wins = list(windows or [])
    notes = _notes()
    checks = []

    def add(name, state, detail, measured=None):
        checks.append({"name": name, "pass": state == "pass", "state": state,
                       "detail": detail, "measured": measured or {}})

    n = len(samples)
    want_s = hours * 3600.0
    if n < 2:
        add("evidence", "incomplete", "有效样本 %d 条，凑不齐起止心跳" % n, {"samples": n})
        return _result(checks, notes, hours, elf_sha, n, {"metrics_load": load_meta or None})
    meta = load_meta or {}
    if meta.get("max_back_step_s", 0.0) > CLOCK_STEP_MAX_S:
        # 主机时钟被回拨过：排序能让曲线连起来，但 up/计数器的先后关系已经不可信，
        # 再往下算只会把「证据不可用」算成「设备复位」（假 FAIL）。不能判就是不能判 → INCOMPLETE。
        add("evidence", "incomplete",
            "主机时间轴不可信：接收时间戳回退 %.0fs（%d 处）/ 容忍 %.0fs，"
            "排序后 uptime 与计数器会表现为回退，属证据不可用，本次不做 PASS/FAIL 判定"
            % (meta["max_back_step_s"], meta.get("out_of_order", 0), CLOCK_STEP_MAX_S),
            {"max_back_step_s": meta["max_back_step_s"], "out_of_order": meta.get("out_of_order", 0)})
        return _result(checks, notes, hours, elf_sha, n, {"metrics_load": meta})

    ts = [s["t"] for s in samples]
    recs = [s["rec"] for s in samples]
    t0 = ts[0]
    # 单调化时间轴：主机时钟小幅回拨时让时间"原地踏步"，而不是把记录重排（重排会让
    # up/计数器看起来在回退）。回拨超过容忍值时上面已经判 INCOMPLETE，走不到这里。
    el = []
    for t in ts:
        step = t - t0
        el.append(step if not el or step >= el[-1] else el[-1])
    board_span = (recs[-1]["up"] - recs[0]["up"]) / 1000.0

    # capture.json 提供的采集侧证据
    cap = capture if isinstance(capture, dict) else None
    cap_complete = bool(cap and cap.get("complete") is True)
    cap_span = None
    if cap and cap.get("started_at") and cap.get("ended_at"):
        try:
            a = datetime.datetime.fromisoformat(str(cap["started_at"]))
            b = datetime.datetime.fromisoformat(str(cap["ended_at"]))
            cap_span = (b - a).total_seconds()
        except ValueError:
            cap_span = None

    def exempt(e):
        """窗口内 + 结束后恢复期：正常联网类判据不评价（其恢复情况由 fault_recovery 判）。"""
        for a, b in wins:
            if a <= e < b + FAULT_RECOVER_S:
                return True
        return False

    normal = [i for i in range(n) if el[i] >= WARMUP_S and not exempt(el[i])]
    nset = set(normal)

    # a1 捕获跨度：不足 = 证据不足（不是"跑得不好"）
    if cap_span is None:
        add("duration_capture", "incomplete", "capture.json 缺 started_at/ended_at，无法算捕获跨度",
            {"capture_span_s": None})
    else:
        add("duration_capture", "pass" if cap_span >= want_s else "incomplete",
            "捕获跨度 %.0fs / 要求 %.0fs" % (cap_span, want_s), {"capture_span_s": cap_span})

    # a2 板端记录跨度
    board_ok = board_span >= want_s - DURATION_SLACK_S
    add("duration_board", "pass" if board_ok else "incomplete",
        "板端 up 跨度 %.1fs / 要求 %.1fs（含 %gs 余量）" % (board_span, want_s, DURATION_SLACK_S),
        {"board_span_s": board_span})

    # a3 固件一致性：跑的不是本地 ELF 的这次构建，后面所有判据都不算数
    mismatch = [i for i in range(n) if recs[i]["fw"] != elf_sha]
    add("firmware_match", "fail" if mismatch else "pass",
        "fw 与本地 ELF 不一致的样本 %d 条（首条 idx=%s）" % (len(mismatch), mismatch[0] if mismatch else None)
        if mismatch else "全部 %d 条样本的 fw 均等于本地 ELF 的 SHA-256" % n,
        {"mismatch": len(mismatch), "first_mismatch_t": ts[mismatch[0]] if mismatch else None})

    # b1 心跳覆盖率（以 10s 标称计）
    if board_span <= 0:
        add("heartbeat_coverage", "incomplete", "板端跨度非正（%.1fs），算不出覆盖率" % board_span,
            {"board_span_s": board_span})
    else:
        cov = n / (board_span / HEARTBEAT_NOMINAL_S)
        add("heartbeat_coverage", "pass" if cov >= COVERAGE_MIN else "fail",
            "覆盖率 %.4f（%d 条 / 标称 %.1f 条）/ 要求 >= %.3f"
            % (cov, n, board_span / HEARTBEAT_NOMINAL_S, COVERAGE_MIN),
            {"coverage": cov, "samples": n})

    # b2 相邻接收间隔
    gaps = [(el[i] - el[i - 1], i) for i in range(1, n)]
    max_gap, gap_at = max(gaps) if gaps else (0.0, 0)
    gap_detail = "最大相邻接收间隔 %.1fs / 上限 %.1fs" % (max_gap, RX_GAP_MAX_S)
    if max_gap > HEARTBEAT_LOST_S:
        gap_detail += "（> %.0fs，心跳中断）" % HEARTBEAT_LOST_S
    add("heartbeat_gap", "pass" if max_gap <= RX_GAP_MAX_S else "fail", gap_detail,
        {"max_gap_s": max_gap, "at_s": el[gap_at]})

    # c 复位 / 异常 / 计数回退
    up_roll = [i for i in range(1, n) if recs[i]["up"] < recs[i - 1]["up"]]
    ok_roll = [i for i in range(1, n) if recs[i]["io"]["ok"] < recs[i - 1]["io"]["ok"]]
    fail_roll = [i for i in range(1, n) if recs[i]["io"]["fail"] < recs[i - 1]["io"]["fail"]]
    rst_bad = [i for i in range(n) if recs[i]["rst"] in ANOMALOUS_RESET]
    ev_bad = [i for i in range(n) if recs[i]["ev"]]
    baseline_rst = recs[0]["rst"]
    rst_switched = [i for i in range(1, n) if recs[i]["rst"] != baseline_rst]
    bad = bool(up_roll or ok_roll or fail_roll or rst_bad or ev_bad or rst_switched)
    add("reset_anomaly", "fail" if bad else "pass",
        "非计划复位 %d（up 回退 %d、rst 与基线 %d 不同 %d）｜异常 rst %d 条｜ev 非空 %d 条"
        "｜io.ok 回退 %d、io.fail 回退 %d"
        % (len(up_roll) + len(rst_switched), len(up_roll), baseline_rst, len(rst_switched),
           len(rst_bad), len(ev_bad), len(ok_roll), len(fail_roll)),
        {"unplanned_resets": len(up_roll) + len(rst_switched), "up_rollbacks": len(up_roll),
         "rst_switched": len(rst_switched), "anomalous_rst": len(rst_bad),
         "ev_nonempty": len(ev_bad), "ok_rollbacks": len(ok_roll), "fail_rollbacks": len(fail_roll),
         "baseline_rst": baseline_rst})

    # d LVGL 存活
    ui_max = max(r["ui"]["age"] for r in recs)
    add("lvgl_alive", "pass" if ui_max <= UI_AGE_MAX_S else "fail",
        "ui.age 最大 %.1fs / 上限 %.1fs" % (ui_max, UI_AGE_MAX_S), {"max_ui_age_s": ui_max})

    # e 栈高水位：0 = 任务没起来（测不到），不是"栈只剩 0 字节"
    stk_min = {k: min(r["stk"][k] for r in recs) for k in ("lvgl", "poll", "pair")}
    unmeasured = [k for k, v in stk_min.items() if v == 0]
    blew = [k for k, v in stk_min.items() if v != 0 and v < STACK_MIN_BYTES]
    stk_txt = "、".join("%s=%d" % (k, stk_min[k]) for k in ("lvgl", "poll", "pair"))
    if blew:
        add("stack_floor", "fail", "栈余量低于 %d 字节：%s（全程最小值 %s）"
            % (STACK_MIN_BYTES, "/".join(blew), stk_txt), dict(stk_min))
    elif unmeasured:
        add("stack_floor", "incomplete", "任务 %s 全程报 0（未启动/测不到），栈判据缺证据"
            % "/".join(unmeasured), dict(stk_min))
    else:
        add("stack_floor", "pass", "全程最小值 %s，均 >= %d 字节" % (stk_txt, STACK_MIN_BYTES),
            dict(stk_min))

    # f 堆底线（逐项，不相加）
    mem_min = {
        "imin": min(r["mem"]["imin"] for r in recs),
        "imax": min(r["mem"]["imax"] for r in recs),
        "dmin": min(r["mem"]["dmin"] for r in recs),
        "pfree": min(r["mem"]["pfree"] for r in recs),
    }
    floors = {"imin": IMIN_MIN_BYTES, "imax": IMAX_MIN_BYTES, "dmin": DMIN_MIN_BYTES,
              "pfree": PFREE_MIN_BYTES}
    under = [k for k in ("imin", "imax", "dmin", "pfree") if mem_min[k] < floors[k]]
    add("heap_floor", "fail" if under else "pass",
        ("击穿：" if under else "全程最小值 ") +
        "、".join("%s=%d(>=%d)" % (k, mem_min[k], floors[k]) for k in ("imin", "imax", "dmin", "pfree")),
        dict(mem_min))

    # g 预热与趋势
    trend = [i for i in range(n) if el[i] >= WARMUP_S]
    head = [i for i in trend if el[i] < WARMUP_S + TREND_WINDOW_S]
    tail = [i for i in trend if el[i] >= el[trend[-1]] - TREND_WINDOW_S] if trend else []
    if not head or not tail:
        add("trend_median", "incomplete", "预热期之后没有足够的首尾窗口（head=%d/tail=%d）"
            % (len(head), len(tail)), {"head": len(head), "tail": len(tail)})
    else:
        med = {}
        for key, path in (("ifree", ("mem", "ifree")), ("imax", ("mem", "imax")),
                          ("pfree", ("mem", "pfree"))):
            h = statistics.median([_pick(recs[i], path) for i in head])
            t = statistics.median([_pick(recs[i], path) for i in tail])
            med[key] = {"head_median": h, "tail_median": t, "loss": h - t}
        over = [k for k in ("ifree", "imax") if med[k]["loss"] > IFREE_LOSS_MAX]
        over += [k for k in ("pfree",) if med[k]["loss"] > PFREE_LOSS_MAX]
        add("trend_median", "fail" if over else "pass",
            "首尾 1h 中位数损失：" + "、".join(
                "%s %.0f→%.0f（-%.0f，上限 %s）" % (k, med[k]["head_median"], med[k]["tail_median"],
                                                   med[k]["loss"],
                                                   IFREE_LOSS_MAX if k != "pfree" else PFREE_LOSS_MAX)
                for k in ("ifree", "imax", "pfree")), med)

    if not trend:
        add("trend_regress", "incomplete", "预热期之后没有样本，回归无输入", {})
    else:
        buckets = {}
        for i in trend:
            buckets.setdefault(int((el[i] - WARMUP_S) // TREND_BUCKET_S), []).append(recs[i])
        if len(buckets) < TREND_MIN_BUCKETS:
            add("trend_regress", "incomplete", "预热后只有 %d 个 10 分钟窗口，不足 %d 个"
                % (len(buckets), TREND_MIN_BUCKETS), {"buckets": len(buckets)})
        else:
            ks = sorted(buckets)
            xs = [k * TREND_BUCKET_S / 3600.0 for k in ks]
            lim = {"ifree": IFREE_SLOPE_MIN, "imax": IMAX_SLOPE_MIN, "pfree": PFREE_SLOPE_MIN}
            slopes = {}
            for key, path in (("ifree", ("mem", "ifree")), ("imax", ("mem", "imax")),
                              ("pfree", ("mem", "pfree"))):
                ys = [statistics.median([_pick(r, path) for r in buckets[k]]) for k in ks]
                slopes[key] = _slope(xs, ys)
            over = [k for k in slopes if slopes[k] < lim[k]]
            add("trend_regress", "fail" if over else "pass",
                "%d 个窗口，" % len(ks) + "、".join(
                    "%s %.2f B/h(>=%.0f)" % (k, slopes[k], lim[k]) for k in ("ifree", "imax", "pfree")),
                slopes)

    # h 正常联网（预热期与故障窗口之外）
    # "测不到"和"测得差"必须分开：io.age 全程 -1 表示板端从未成功接收过一帧，
    # 此时联网判据没有可评价的对象（NAS 全程不可达 ≠ 固件有问题）→ INCOMPLETE，
    # 只有"部分失败/变旧"才是能判的 FAIL。同理全 0 的成功率分母也是 0。
    connected = [i for i in normal if recs[i]["io"]["age"] >= 0]
    if not normal:
        add("net_health", "incomplete", "预热期与故障窗口之外没有样本，联网判据缺证据",
            {"normal_samples": 0})
    elif not connected:
        add("net_health", "incomplete",
            "正常窗口内 %d 条样本的 io.age 全为 -1（板端从未成功接收），联网判据无证据，"
            "不能当成 PASS 也不能当成 FAIL" % len(normal),
            {"normal_samples": len(normal), "connected_samples": 0})
    else:
        d_ok = sum(max(0, recs[i]["io"]["ok"] - recs[i - 1]["io"]["ok"])
                   for i in normal if i - 1 in nset)
        d_fail = sum(max(0, recs[i]["io"]["fail"] - recs[i - 1]["io"]["fail"])
                     for i in normal if i - 1 in nset)
        rate = d_ok / (d_ok + d_fail) if (d_ok + d_fail) else 0.0
        fr = sum(1 for i in normal if fresh(recs[i])) / len(normal)
        age_max = max(recs[i]["io"]["age"] for i in normal)
        p95_max = max(recs[i]["io"]["p95"] for i in normal)
        bad = []
        if rate < POLL_OK_RATE_MIN:
            bad.append("成功率 %.4f < %.3f" % (rate, POLL_OK_RATE_MIN))
        if fr < FRESH_RATIO_MIN:
            bad.append("新鲜比例 %.4f < %.3f" % (fr, FRESH_RATIO_MIN))
        if age_max > IO_AGE_MAX_S:
            bad.append("io.age 最大 %.1fs > %.1fs" % (age_max, IO_AGE_MAX_S))
        if p95_max > P95_MAX_MS:
            bad.append("io.p95 最大 %dms > %dms" % (p95_max, P95_MAX_MS))
        add("net_health", "fail" if bad else "pass",
            ("；".join(bad) if bad else "正常窗口内 %d 条样本：成功率 %.4f、新鲜比例 %.4f、"
             "io.age 最大 %.1fs、io.p95 最大 %dms" % (len(normal), rate, fr, age_max, p95_max)),
            {"normal_samples": len(normal), "poll_ok": d_ok, "poll_fail": d_fail,
             "success_rate": rate, "fresh_ratio": fr, "max_io_age_s": age_max, "max_p95_ms": p95_max})

    # i 来源采样推进（防止 200 秒前的旧帧被当成在线）
    missing_src = 0
    frozen_max = 0.0
    run_start = None
    prev = None
    for i in normal:
        src = recs[i]["io"]["src"]
        if src < 0:
            missing_src += 1
            run_start = prev = None
            continue
        if prev is not None and i == prev + 1 and src == recs[prev]["io"]["src"]:
            frozen_max = max(frozen_max, el[i] - el[run_start])
        else:
            run_start = i
        prev = i
    src_bad = bool(missing_src) or frozen_max > SRC_STALL_MAX_S
    if src_bad and not connected:
        # 一帧都没成功过：没有"旧帧冒充在线"这回事，属证据不可用（与 net_health 同一根因）。
        add("source_progress", "incomplete",
            "正常窗口内 io.src 全为 -1（从未收到成功帧），来源推进无法评价",
            {"missing_src": missing_src, "max_frozen_s": frozen_max})
    else:
        add("source_progress", "fail" if src_bad else "pass",
            "io.src 缺失 %d 条；同一值最长持续 %.1fs / 上限 %.1fs"
            % (missing_src, frozen_max, SRC_STALL_MAX_S),
            {"missing_src": missing_src, "max_frozen_s": frozen_max})

    # j 故障恢复
    if not wins:
        add("fault_recovery", "pass", "无登记故障窗口", {"windows": 0})
    else:
        unrec = []
        took = []
        base_missing = 0
        for a, b in wins:
            base = None
            for i in range(n):
                if el[i] <= b:
                    base = i
                else:
                    break
            if base is None:
                base_missing += 1
                took.append(None)
                continue
            b_ok = recs[base]["io"]["ok"]
            b_src = recs[base]["io"]["src"]
            found = None
            for i in range(n):
                # 恢复证据必须**严格晚于**窗口 end：落在窗口内的样本不算恢复，
                # 否则"窗口一直烂到最后一秒"也会因为末尾那条样本被判成已恢复。
                if el[i] <= b:
                    continue
                if el[i] > b + FAULT_RECOVER_S:
                    break
                src_adv = recs[i]["io"]["src"] >= 0 and (b_src < 0 or recs[i]["io"]["src"] > b_src)
                if recs[i]["io"]["ok"] > b_ok or src_adv:
                    found = el[i] - b
                    break
            took.append(found)
            if found is None:
                unrec.append([a, b])
        if unrec:
            add("fault_recovery", "fail",
                "窗口结束后 %.0fs 内没有新成功计数/推进的 io.src：%s"
                % (FAULT_RECOVER_S, unrec), {"unrecovered": unrec, "recover_s": took})
        elif base_missing:
            add("fault_recovery", "incomplete",
                "%d 个窗口在首条样本之前就结束了，没有基线可比" % base_missing,
                {"recover_s": took})
        else:
            add("fault_recovery", "pass",
                "%d 个窗口均在 %.0fs 内恢复（耗时 %s）"
                % (len(wins), FAULT_RECOVER_S, ["%.0fs" % x for x in took]),
                {"recover_s": took})

    # k 证据
    missing_bits = []
    if not cap_complete:
        missing_bits.append("缺结束标记（capture.json 缺失或 complete != true）")
    if cap_span is None:
        missing_bits.append("capture.json 缺 started_at/ended_at")
    floor_n = int(want_s / HEARTBEAT_NOMINAL_S * MIN_SAMPLE_RATIO)
    if n < floor_n:
        missing_bits.append("有效样本 %d < 下限 %d" % (n, floor_n))
    if not trend:
        missing_bits.append("预热期（%ds）之后没有样本，无内存证据" % int(WARMUP_S))
    if cap and type(cap.get("sample_count")) is int and cap["sample_count"] != n:
        missing_bits.append("capture.json 记的样本数 %d 与实际 %d 不一致" % (cap["sample_count"], n))
    meta = load_meta or {}
    if meta.get("malformed_tail"):
        missing_bits.append("metrics.jsonl 末尾有 %d 行残缺记录（采集被强杀？），未计入证据"
                            % meta["malformed_tail"])
    add("evidence", "incomplete" if missing_bits else "pass",
        "；".join(missing_bits) if missing_bits
        else "结束标记、起止心跳、样本量（%d）、预热后证据齐备" % n,
        {"samples": n, "min_samples": floor_n, "complete": cap_complete})

    extra = {
        "first_t": t0, "last_t": ts[-1], "board_span_s": board_span,
        "capture_span_s": cap_span, "fault_windows": [list(w) for w in wins],
        "metrics_load": meta or None,
        "capture": {"complete": cap_complete,
                    "parse_drop": cap.get("parse_drop") if cap else None,
                    "parse_drop_run_max": cap.get("parse_drop_run_max") if cap else None,
                    "sample_count": cap.get("sample_count") if cap else None},
    }
    return _result(checks, notes, hours, elf_sha, n, extra)


# ---------------------------------------------------------------- capture
def _now_iso():
    return datetime.datetime.now().astimezone().isoformat(timespec="milliseconds")


def _write_json(path, obj):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, ensure_ascii=False, indent=2, sort_keys=True)
        fh.write("\n")


_SECRET_FLAGS = {"--port": "<port>", "--elf": "<elf>", "--output": "<output>",
                 "--input": "<input>", "--fault-windows": "<windows>"}


def sanitized_argv(argv):
    """argv 脱敏：路径/设备名换成占位符，产物里不留家目录、串口路径、主机名。"""
    out = []
    expect = None
    for a in argv:
        if expect is not None:
            out.append(expect)
            expect = None
            continue
        flag, eq, _ = a.partition("=")
        if flag in _SECRET_FLAGS:
            if eq:
                out.append(flag + "=" + _SECRET_FLAGS[flag])
            else:
                out.append(a)
                expect = _SECRET_FLAGS[flag]
            continue
        out.append(os.path.basename(a) if os.sep in a else a)
    return out


def open_readonly(port_name, baud=BAUDRATE):
    """只读打开串口：DTR/RTS 在 open() **之前**就置 false，且此后不再碰任何控制线。

    pyserial 默认 dtr=True/rts=True，而 ESP32-P4 的 USB-Serial-JTAG 把 DTR/RTS 当 BOOT/EN：
    打开端口的瞬间拉高就把板子复位（甚至停在下载模式），长测会在开始的第一秒被打断，
    看上去却像"板子自己重启"。pyserial 的 open() 会按打开前的 _dtr_state/_rts_state 初始化
    （前提是 dsrdtr/rtscts 都为 False），所以必须先 setattr 再 open。
    """
    import serial                      # 只有 capture 路径需要 pyserial；analyze/测试不依赖它

    port = serial.Serial()
    port.port = port_name
    port.baudrate = baud
    port.timeout = 1.0
    port.rtscts = False
    port.dsrdtr = False
    port.dtr = False
    port.rts = False
    port.open()
    return port


def capture_main(args, argv):
    port_name = args.port or os.environ.get(ENV_PORT)
    if not port_name:
        sys.stderr.write(
            "用法: soak72.py capture --hours 72 --elf build/fnos_monitor.elf --output <目录> "
            "--port <串口>\n"
            "  端口也可以放在环境变量 %s 里（本进程没读到）。\n" % ENV_PORT)
        return EXIT_INPUT_ERROR
    if not os.path.isfile(args.elf):
        sys.stderr.write("--elf 不存在或不是文件：%s\n" % os.path.basename(args.elf))
        return EXIT_INPUT_ERROR
    try:
        windows = load_windows(args.fault_windows)
        elf_sha = sha256_file(args.elf)
    except InputError as exc:
        sys.stderr.write("输入错误：%s\n" % exc)
        return EXIT_INPUT_ERROR
    except OSError as exc:
        sys.stderr.write("读不了 --elf：%s\n" % type(exc).__name__)
        return EXIT_INPUT_ERROR

    # 先确保产物目录可写，再去碰串口：否则会因为"目录写不了"白开一次串口，
    # 而开串口本身对板子就是有副作用的一次电气动作。
    try:
        os.makedirs(args.output, exist_ok=True)
    except OSError as exc:
        sys.stderr.write("创建输出目录失败：%s\n" % type(exc).__name__)
        return EXIT_INPUT_ERROR
    try:
        port = open_readonly(port_name, BAUDRATE)
    except Exception as exc:                                  # noqa: BLE001
        sys.stderr.write("打不开串口：%s（不重试、不降级）\n" % exc)
        return EXIT_INPUT_ERROR

    metrics_path = os.path.join(args.output, "metrics.jsonl")
    capture_path = os.path.join(args.output, "capture.json")
    started_at = _now_iso()
    ended_at = None
    state = {"complete": False, "sample_count": 0, "parse_drop": 0, "parse_drop_run_max": 0}
    errors = []
    drop_run = 0
    next_report = time.monotonic() + 3600.0

    def write_capture():
        _write_json(capture_path, {
            "tool": "soak72 capture",
            "complete": state["complete"],
            "hours": args.hours,
            "baudrate": BAUDRATE,
            "port": "serial",              # 匿名化：产物里不留设备路径
            "elf_sha256": elf_sha,
            "started_at": started_at,
            "ended_at": ended_at,
            "sample_count": state["sample_count"],
            "parse_drop": state["parse_drop"],
            "parse_drop_run_max": state["parse_drop_run_max"],
            "fault_windows": {"file": os.path.basename(args.fault_windows) if args.fault_windows else None,
                              "count": len(windows)},
            "argv": sanitized_argv(argv),
            "errors": errors,
        })

    # 先落一份 complete=false：进程被 kill 也不会留下"看起来跑完了"的证据
    write_capture()
    print("采集 %s：%s（%gh）→ %s" % (port_name, MARK, args.hours, os.path.basename(metrics_path)),
          flush=True)

    try:
        with open(metrics_path, "w", encoding="utf-8") as fh:
            deadline = time.monotonic() + args.hours * 3600.0
            buf = b""
            while time.monotonic() < deadline:
                chunk = port.read(4096)
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    raw, _, buf = buf.partition(b"\n")
                    rec = parse_line(raw.decode("utf-8", "replace").rstrip("\r"))
                    if rec is None:
                        # 文本行/截断行：丢弃并计数。截断是环形缓冲溢出的正常后果，
                        # 不能让它把整份采集搞崩，也不能写进证据文件。
                        state["parse_drop"] += 1
                        drop_run += 1
                        state["parse_drop_run_max"] = max(state["parse_drop_run_max"], drop_run)
                        continue
                    drop_run = 0
                    fh.write(json.dumps({"t": round(time.time(), 3), "rec": rec},
                                        ensure_ascii=False, separators=(",", ":")) + "\n")
                    fh.flush()
                    state["sample_count"] += 1
                if time.monotonic() >= next_report:
                    next_report += 3600.0
                    print("  已采集 %.1fh：样本 %d 条，丢弃 %d 行（最长连续 %d）"
                          % ((time.monotonic() - (deadline - args.hours * 3600.0)) / 3600.0,
                             state["sample_count"], state["parse_drop"], state["parse_drop_run_max"]),
                          flush=True)
            if buf.strip():
                # 收工时残在半行缓冲区里：不猜它的后半截
                state["parse_drop"] += 1
                state["parse_drop_run_max"] = max(state["parse_drop_run_max"], drop_run + 1)
            state["complete"] = True
    except KeyboardInterrupt:
        errors.append({"at": _now_iso(), "event": "interrupted"})
    except Exception as exc:                                  # noqa: BLE001
        # 掉线/读失败：记一条 error 就收工。只记异常类型——pyserial 的消息里带设备路径。
        errors.append({"at": _now_iso(), "event": "serial_read_failed", "exc": type(exc).__name__})
        print("串口读失败：%s（不重连，直接收工）" % type(exc).__name__, file=sys.stderr, flush=True)
    finally:
        ended_at = _now_iso()
        try:
            port.close()
        except Exception:                                     # noqa: BLE001
            pass
        write_capture()

    if state["complete"]:
        print("完成：样本 %d 条，丢弃 %d 行（最长连续 %d），结束标记 complete=true"
              % (state["sample_count"], state["parse_drop"], state["parse_drop_run_max"]))
        return EXIT_PASS
    print("未完成：样本 %d 条，丢弃 %d 行，complete=false（analyze 会判 INCOMPLETE）"
          % (state["sample_count"], state["parse_drop"]), file=sys.stderr)
    return EXIT_FAIL


# ---------------------------------------------------------------- analyze
def _print_report(result, metrics_name):
    print("=== soak72 复核：%s ===" % metrics_name)
    cap = result.get("capture") or {}
    print("样本 %d 条｜板端跨度 %ss｜捕获跨度 %ss｜解析丢弃 %s 行（最长连续 %s）"
          % (result["sample_count"], _fmt(result.get("board_span_s")),
             _fmt(result.get("capture_span_s")), cap.get("parse_drop"), cap.get("parse_drop_run_max")))
    mark = {"pass": "✓", "fail": "✗", "incomplete": "?"}
    for c in result["checks"]:
        print("%s %-18s %s" % (mark[c["state"]], c["name"], c["detail"]))
    why = {"PASS": "全部判据成立", "FAIL": "有判据被证据证伪",
           "INCOMPLETE": "证据不足，不能判 PASS",
           "INPUT_ERROR": "输入不可用，没有做出任何评价"}
    print("结论：%s（%s）" % (result["verdict"], why.get(result["verdict"], "未知")))
    for f in result["failures"]:
        print("  失败：%s" % f)
    for f in result["incomplete_reasons"]:
        print("  缺证据：%s" % f)
    for note in result["notes"]:
        print("  说明：%s" % note)


def _fmt(v):
    return "N/A" if v is None else "%.1f" % v


def _write_summary(output, result):
    """summary.json 是唯一的下游接口：连"输入坏了"也必须落一份，绝不静默不写。"""
    try:
        os.makedirs(output, exist_ok=True)
        _write_json(os.path.join(output, "summary.json"), result)
        return True
    except Exception as exc:                                  # noqa: BLE001
        sys.stderr.write("summary.json 写不进去：%s\n" % type(exc).__name__)
        return False


def analyze_main(args):
    """读文件 → 评价 → 落 summary。除了四个退出码，不向外抛任何异常。"""
    reason = None
    result = None
    try:
        if not os.path.isfile(args.elf):
            raise InputError("--elf 不存在或不是文件")
        elf_sha = sha256_file(args.elf)
        windows = load_windows(args.fault_windows)
        samples, load_meta = load_metrics(args.input)
        capture = load_capture(os.path.join(os.path.dirname(os.path.abspath(args.input)),
                                            "capture.json"))
        result = evaluate(samples, capture, args.hours, elf_sha, windows, load_meta)
    except InputError as exc:
        reason = str(exc)
    except OSError as exc:
        reason = "输入文件读不了：%s" % type(exc).__name__
    except Exception as exc:                                  # noqa: BLE001
        # 判据代码自己出问题也算"这次没结论"，但不能抛栈：长测证据只有一份，
        # 崩溃会让下游以为"程序没跑"，而 3 明确说的是"输入/参数有问题"。
        reason = "复核过程出错：%s" % type(exc).__name__
    if reason is None:
        _write_summary(args.output, result)
        _print_report(result, os.path.basename(args.input))
        print("summary: summary.json")
        return VERDICT_EXIT[result["verdict"]]
    result = {"tool": "soak72 analyze", "verdict": "INPUT_ERROR", "hours": args.hours,
              "elf_sha256": None, "sample_count": 0, "checks": [], "failures": [],
              "incomplete_reasons": [reason], "notes": _notes(), "input_error": reason}
    _write_summary(args.output, result)
    sys.stderr.write("输入错误：%s\n" % reason)
    return EXIT_INPUT_ERROR


# ---------------------------------------------------------------- CLI
class _Parser(argparse.ArgumentParser):
    """参数错误统一退 3（INPUT_ERROR），不出现 argparse 默认的 2。"""

    def error(self, message):
        self.print_usage(sys.stderr)
        sys.stderr.write("参数错误: %s\n" % message)
        raise SystemExit(EXIT_INPUT_ERROR)


def _build_parser():
    ap = _Parser(prog="soak72.py", description="72 小时实机长测的只读采集与离线复核")
    sub = ap.add_subparsers(dest="cmd")

    cap = sub.add_parser("capture", help="只读抓串口，只落数值记录")
    cap.add_argument("--hours", type=float, required=True, help="采集时长（小时），到点自动收尾")
    cap.add_argument("--elf", required=True, help="本次烧录的 ELF，用于记录 SHA-256")
    cap.add_argument("--output", required=True, help="输出目录")
    cap.add_argument("--port", default=None, help="串口设备；缺省读环境变量 %s" % ENV_PORT)
    cap.add_argument("--fault-windows", default=None, help="故障窗口 JSON（只登记，不产生故障）")

    ana = sub.add_parser("analyze", help="离线按固定判据评价")
    ana.add_argument("--input", required=True, help="metrics.jsonl")
    ana.add_argument("--hours", type=float, required=True, help="要求的采集时长（小时）")
    ana.add_argument("--elf", required=True, help="本地 ELF，逐条比对 fw")
    ana.add_argument("--output", required=True, help="输出目录（写 summary.json）")
    ana.add_argument("--fault-windows", default=None, help="故障窗口 JSON")
    return ap


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    args = _build_parser().parse_args(argv)
    try:
        if args.cmd == "capture":
            return capture_main(args, [os.path.basename(sys.argv[0])] + argv)
        if args.cmd == "analyze":
            return analyze_main(args)
    except KeyboardInterrupt:
        sys.stderr.write("被中断。\n")
        return EXIT_FAIL
    except Exception as exc:                                  # noqa: BLE001
        # 兜底：退出码只有 0/1/2/3 四种含义，未预料到的异常不能变成 traceback+退出码 1
        # （那会被当成"长测失败"）。这里是输入/环境问题，报 3 并只打类型名。
        sys.stderr.write("未预料的错误：%s\n" % type(exc).__name__)
        return EXIT_INPUT_ERROR
    _build_parser().print_help()
    return EXIT_INPUT_ERROR


if __name__ == "__main__":
    sys.exit(main())
