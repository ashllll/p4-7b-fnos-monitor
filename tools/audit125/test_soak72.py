#!/usr/bin/env python3
"""soak72 判据的自测：判据必须能被合成的坏数据证伪，否则它只是装饰。

长测只有一次机会（72 小时），判据写错就得重跑三天；而真机上很难复现的坏事（panic 重启、
栈击穿、200 秒旧帧假在线、故障窗口后不恢复）必须在这里先证明"它真的会被抓住"。
所以本文件不碰硬件、不 import serial：只用合成 metrics.jsonl 调 soak72 的纯函数与
analyze/capture 入口，并覆盖阈值两侧（恰好等于阈值必须 PASS，越过一档必须 FAIL）。
"""
import argparse
import contextlib
import datetime
import importlib.util
import io
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("soak72", os.path.join(HERE, "soak72.py"))
soak72 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(soak72)

TMP = tempfile.mkdtemp(prefix="soak72-selftest-")
ELF = os.path.join(TMP, "fake-fnos_monitor.elf")
with open(ELF, "wb") as _fh:
    _fh.write(b"\x7fELF" + bytes(range(256)) * 64)
ELF_SHA = soak72.sha256_file(ELF)
BAD_SHA = "ab" * 32
SRC0 = 1697000000
WINDOW_A, WINDOW_B = 24 * 3600.0, 25 * 3600.0     # 故障窗口测试用：24h~25h

COUNT = 0
FAILED = []


def check(name, ok, detail=""):
    """一条用例 = 一个判据方向。失败时把实测值带上，免得回头再猜。"""
    global COUNT
    COUNT += 1
    print(("PASS %s" % name) if ok else ("FAIL %s  — %s" % (name, detail)))
    if not ok:
        FAILED.append((name, detail))
    return ok


# ---------------------------------------------------------------- 合成数据
def build(hours=72.0, *, fw=None, dt=10.0, drift=(-20.0, -1000.0), mutate=None, drop=()):
    """合成一段"常态健康"的心跳序列，mutate(i, el, rec) 负责注入异常。

    常态刻意选成缓慢漂移（内部 -20 B/h、PSRAM -1000 B/h）：这样才能验证趋势判据不会
    把正常的长测误判成泄漏——只造"完全平坦"的数据是测不出趋势判据的松紧的。
    """
    fw = fw or ELF_SHA
    total = int(round(hours * 3600.0 / dt))
    out = []
    for i in range(total + 1):
        el = i * dt
        rec = {
            "n": i + 1, "up": i * 10000, "hz": 10, "fw": fw, "rst": 1, "ev": [],
            "io": {"ok": i, "fail": 0, "age": 0, "p95": 200, "src": SRC0 + i * 10},
            "ui": {"age": 1},
            "mem": {"ifree": int(300000 + drift[0] * el / 3600.0), "imin": 200000, "imax": 40000,
                    "dfree": 60000, "dmin": 20000,
                    "pfree": int(3000000 + drift[1] * el / 3600.0), "pmin": 2500000},
            "stk": {"lvgl": 3000, "poll": 2100, "pair": 1800},
        }
        if mutate is not None:
            mutate(i, el, rec)
        if i in drop:
            continue
        out.append({"t": el, "rec": rec})
    return out


def write_metrics(path, samples):
    with open(path, "w", encoding="utf-8") as fh:
        for s in samples:
            fh.write(json.dumps(s, ensure_ascii=False, separators=(",", ":")) + "\n")


def write_capture(path, samples, hours, *, span=None, complete=True, sample_count=None,
                  parse_drop=0, parse_drop_run_max=0):
    start = datetime.datetime(2026, 1, 1, 12, 0, 0)
    span = hours * 3600.0 + 5.0 if span is None else span
    end = start + datetime.timedelta(seconds=span)
    obj = {"tool": "soak72 capture", "complete": bool(complete), "hours": hours, "baudrate": 115200,
           "port": "serial", "elf_sha256": ELF_SHA,
           "started_at": start.isoformat(timespec="milliseconds"),
           "ended_at": end.isoformat(timespec="milliseconds"),
           "sample_count": len(samples) if sample_count is None else sample_count,
           "parse_drop": parse_drop, "parse_drop_run_max": parse_drop_run_max,
           "fault_windows": {"file": None, "count": 0}, "argv": ["soak72.py"], "errors": []}
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, ensure_ascii=False, indent=2)


def run_case(name, samples, hours=4.0, *, windows=None, complete=True, capture=True, span=None,
             elf=None, raw_metrics=None, capture_extra=None):
    """把一个合成数据集喂给 analyze 入口（走真实的读文件/退码/写 summary 路径）。"""
    d = os.path.join(TMP, name)
    os.makedirs(d, exist_ok=True)
    metrics = os.path.join(d, "metrics.jsonl")
    if raw_metrics is None:
        write_metrics(metrics, samples)
    else:
        with open(metrics, "w", encoding="utf-8") as fh:
            fh.write(raw_metrics)
    if capture:
        write_capture(os.path.join(d, "capture.json"), samples, hours, span=span, complete=complete,
                      **(capture_extra or {}))
    wpath = None
    if windows is not None:
        wpath = os.path.join(d, "windows.json")
        with open(wpath, "w", encoding="utf-8") as fh:
            json.dump(windows, fh)
    out = os.path.join(d, "review")
    args = argparse.Namespace(**{"input": metrics, "hours": hours, "elf": elf or ELF,
                                 "output": out, "fault_windows": wpath})
    with contextlib.redirect_stdout(io.StringIO()):
        code = soak72.analyze_main(args)
    spath = os.path.join(out, "summary.json")
    if os.path.exists(spath):
        with open(spath, encoding="utf-8") as fh:
            summary = json.load(fh)
    else:
        # 退 3 的用例本来就不该产出 summary；给个空壳，让 check 打印实测的退出码
        summary = {"verdict": None, "checks": [], "failures": [], "notes": []}
    return code, summary


def state_of(summary, name):
    for c in summary["checks"]:
        if c["name"] == name:
            return c["state"]
    return "missing"


def detail_of(summary, name):
    for c in summary["checks"]:
        if c["name"] == name:
            return c["detail"]
    return "缺这条检查"


def sample_lines():
    """两行合法诊断（带 IDF 前缀）+ 两行坏行（截断 / 纯文本）。"""
    good = ('I (123456) main: FNOS_DIAG {"n":1,"up":10000,"hz":10,"fw":"%s","rst":1,"ev":[],'
            '"io":{"ok":1,"fail":0,"age":0,"p95":180,"src":1697000000},'
            '"ui":{"age":0},'
            '"mem":{"ifree":300000,"imin":200000,"imax":40000,"dfree":60000,"dmin":20000,'
            '"pfree":3000000,"pmin":2500000},'
            '"stk":{"lvgl":3000,"poll":2100,"pair":1800}}' % ELF_SHA)
    good2 = good.replace('"n":1', '"n":2').replace('"up":10000', '"up":20000')
    return [good, good2]


# ---------------------------------------------------------------- 采集侧纯函数
class FakePort:
    """假串口：证明采集侧"只读、不重连、只落数值"三条契约。"""

    def __init__(self, chunks, raise_after=None):
        self.chunks = list(chunks)
        self.raise_after = raise_after
        self.reads = 0
        self.closed = False

    def read(self, size=4096):
        if self.raise_after is not None and self.reads >= self.raise_after:
            raise OSError("device reports readiness to read but returned no data: /dev/cu.usbmodem1101")
        self.reads += 1
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, data):
        raise AssertionError("采集侧禁止向串口写字节")

    def close(self):
        self.closed = True


def capture_case(name, chunks, *, port="/dev/cu.usbmodem1101", hours=0.0001, raise_after=None,
                 elf=None, windows=None):
    """跑一次真实 capture 入口（只把串口换成 FakePort），返回 (退出码, capture.json, 目录, 开端口次数)。"""
    d = os.path.join(TMP, name)
    os.makedirs(d, exist_ok=True)
    orig = soak72.open_readonly
    fake = FakePort(chunks, raise_after=raise_after)
    opened = []

    def fake_open(p, baud=115200):
        opened.append(p)
        return fake

    soak72.open_readonly = fake_open
    args = argparse.Namespace(port=port, elf=elf or ELF, output=d, hours=hours,
                              fault_windows=windows)
    try:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            code = soak72.capture_main(args, ["soak72.py", "capture", "--port", port,
                                              "--elf", ELF, "--output", d])
    finally:
        soak72.open_readonly = orig
    cpath = os.path.join(d, "capture.json")
    cap = None
    if os.path.exists(cpath):
        with open(cpath, encoding="utf-8") as fh:
            cap = json.load(fh)
    return code, cap, d, len(opened)


def privacy_scan(path):
    """产物里不得出现的原始信息。"""
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    bad = [needle for needle in ("/dev/", "usbmodem", "/Users/", "COM3", "FNOS_DIAG")
           if needle in text]
    return bad


# ---------------------------------------------------------------- 用例
def main():
    # --- 1. 正常 72h：必须 PASS，不能因为判据过敏把好数据判死
    code, s = run_case("normal72", build(72.0), hours=72.0)
    check("normal_72h_pass", code == 0 and s["verdict"] == "PASS",
          "code=%s verdict=%s failures=%s" % (code, s["verdict"], s["failures"]))

    # --- 2. 阈值两侧：恰好等于阈值要 PASS（不然判据会把"卡线"误判成故障）
    def at_threshold(i, el, rec):
        # 单位是秒/字节的整数字段，卡在阈值上必须 PASS（阈值比较不做隐式取整）
        rec["ui"]["age"] = int(soak72.UI_AGE_MAX_S)
        rec["io"]["age"] = int(soak72.IO_AGE_MAX_S)
        rec["io"]["p95"] = soak72.P95_MAX_MS
        rec["stk"] = {"lvgl": soak72.STACK_MIN_BYTES, "poll": soak72.STACK_MIN_BYTES,
                      "pair": soak72.STACK_MIN_BYTES}
        rec["mem"].update({"ifree": 300000, "imin": soak72.IMIN_MIN_BYTES,
                           "imax": soak72.IMAX_MIN_BYTES, "dfree": 60000,
                           "dmin": soak72.DMIN_MIN_BYTES, "pfree": soak72.PFREE_MIN_BYTES,
                           "pmin": soak72.PFREE_MIN_BYTES})

    code, s = run_case("threshold_ok", build(4.0, drift=(0.0, 0.0), mutate=at_threshold), hours=4.0)
    check("thresholds_exact_pass", code == 0 and s["verdict"] == "PASS",
          "code=%s verdict=%s failures=%s" % (code, s["verdict"], s["failures"]))

    # --- 3. 时长不足 → INCOMPLETE（不是 PASS，也不是 FAIL）
    code, s = run_case("short", build(6.0), hours=72.0, span=6.0 * 3600.0 + 5.0)
    check("short_duration_incomplete", code == 2 and s["verdict"] == "INCOMPLETE",
          "code=%s verdict=%s" % (code, s["verdict"]))
    check("short_duration_checks", state_of(s, "duration_capture") == "incomplete"
          and state_of(s, "duration_board") == "incomplete",
          "%s / %s" % (detail_of(s, "duration_capture"), detail_of(s, "duration_board")))

    # --- 4. 缺结束标记 → INCOMPLETE（缺证据一律不许换 PASS）
    code, s = run_case("nocomplete", build(4.0), hours=4.0, complete=False)
    check("missing_endmarker_incomplete",
          code == 2 and s["verdict"] == "INCOMPLETE" and state_of(s, "evidence") == "incomplete",
          "code=%s verdict=%s evidence=%s" % (code, s["verdict"], detail_of(s, "evidence")))

    # --- 5. up 回退 = 非计划复位 → FAIL
    def rollback(i, el, rec):
        if i >= 400:
            rec["up"] = (i - 400) * 10000

    code, s = run_case("uproll", build(4.0, mutate=rollback), hours=4.0)
    check("uptime_rollback_fail", code == 1 and state_of(s, "reset_anomaly") == "fail",
          "code=%s %s" % (code, detail_of(s, "reset_anomaly")))

    # --- 6. rst 与基线不同 = 非计划复位 → FAIL
    def rst_shift(i, el, rec):
        if i >= 400:
            rec["rst"] = 3

    code, s = run_case("rstshift", build(4.0, mutate=rst_shift), hours=4.0)
    check("rst_switch_fail", code == 1 and state_of(s, "reset_anomaly") == "fail",
          "code=%s %s" % (code, detail_of(s, "reset_anomaly")))

    # --- 7. rst=4 panic → FAIL，包括第一条样本（板子刚崩过就该判负）
    def panic_all(i, el, rec):
        rec["rst"] = 4

    code, s = run_case("panic", build(4.0, mutate=panic_all), hours=4.0)
    check("rst_panic_fail", code == 1 and state_of(s, "reset_anomaly") == "fail",
          "code=%s %s" % (code, detail_of(s, "reset_anomaly")))

    # --- 8. ev 非空 → FAIL
    def ev_at(i, el, rec):
        if i == 500:
            rec["ev"] = ["panic"]

    code, s = run_case("ev", build(4.0, mutate=ev_at), hours=4.0)
    check("ev_nonempty_fail", code == 1 and state_of(s, "reset_anomaly") == "fail",
          "code=%s %s" % (code, detail_of(s, "reset_anomaly")))

    # --- 9. io.ok 回退 → FAIL
    def ok_roll(i, el, rec):
        if i >= 400:
            rec["io"]["ok"] = i - 400

    code, s = run_case("okroll", build(4.0, mutate=ok_roll), hours=4.0)
    check("poll_counter_rollback_fail", code == 1 and state_of(s, "reset_anomaly") == "fail",
          "code=%s %s" % (code, detail_of(s, "reset_anomaly")))

    # --- 10. 相邻接收间隔 40s → FAIL（>30s）
    code, s = run_case("gap", build(4.0, drop=range(400, 404)), hours=4.0)
    check("rx_gap_fail", code == 1 and state_of(s, "heartbeat_gap") == "fail",
          "code=%s %s" % (code, detail_of(s, "heartbeat_gap")))

    # --- 11. 心跳中断 >60s → FAIL，且覆盖率随之下滑
    code, s = run_case("lost", build(4.0, drop=range(400, 415)), hours=4.0)
    check("heartbeat_lost_fail", code == 1 and state_of(s, "heartbeat_gap") == "fail",
          "code=%s %s" % (code, detail_of(s, "heartbeat_gap")))

    # --- 12. ui.age 超时 → FAIL（LVGL 卡死）
    def ui_stuck(i, el, rec):
        if i >= 300:
            rec["ui"]["age"] = int(soak72.UI_AGE_MAX_S) + 1

    code, s = run_case("ui", build(4.0, mutate=ui_stuck), hours=4.0)
    check("lvgl_stuck_fail", code == 1 and state_of(s, "lvgl_alive") == "fail",
          "code=%s %s" % (code, detail_of(s, "lvgl_alive")))

    # --- 13. 栈余量击穿 → FAIL
    def stack_low(i, el, rec):
        if i >= 300:
            rec["stk"]["poll"] = soak72.STACK_MIN_BYTES - 1

    code, s = run_case("stacklow", build(4.0, mutate=stack_low), hours=4.0)
    check("stack_floor_fail", code == 1 and state_of(s, "stack_floor") == "fail",
          "code=%s %s" % (code, detail_of(s, "stack_floor")))

    # --- 14. 栈报 0 = 任务没起来/测不到 → INCOMPLETE（不是"只剩 0 字节"的 FAIL）
    def stack_zero(i, el, rec):
        rec["stk"]["pair"] = 0

    code, s = run_case("stackzero", build(72.0, mutate=stack_zero), hours=72.0)
    check("stack_unmeasured_incomplete",
          code == 2 and s["verdict"] == "INCOMPLETE" and state_of(s, "stack_floor") == "incomplete",
          "code=%s verdict=%s %s" % (code, s["verdict"], detail_of(s, "stack_floor")))

    # --- 15. 堆底线击穿（内部历史最小空闲）→ FAIL
    def heap_low(i, el, rec):
        if i >= 300:
            rec["mem"]["imin"] = soak72.IMIN_MIN_BYTES - 1

    code, s = run_case("heaplow", build(4.0, mutate=heap_low), hours=4.0)
    check("heap_floor_fail", code == 1 and state_of(s, "heap_floor") == "fail",
          "code=%s %s" % (code, detail_of(s, "heap_floor")))

    # --- 16. PSRAM 空闲底线击穿 → FAIL
    def psram_low(i, el, rec):
        rec["mem"]["pfree"] = soak72.PFREE_MIN_BYTES - 1

    code, s = run_case("psramlow", build(4.0, mutate=psram_low), hours=4.0)
    check("psram_floor_fail", code == 1 and state_of(s, "heap_floor") == "fail",
          "code=%s %s" % (code, detail_of(s, "heap_floor")))

    # --- 17. 趋势泄漏：内部空闲 1KiB/h 稳定下滑 → FAIL
    code, s = run_case("leak", build(4.0, drift=(-1024.0, -1000.0)), hours=4.0)
    check("trend_regress_fail", code == 1 and state_of(s, "trend_regress") == "fail",
          "code=%s %s" % (code, detail_of(s, "trend_regress")))

    # --- 18. io.p95 超 1000ms → FAIL（尖峰放在预热期之外，否则被趋势窗口豁免）
    def p95_high(i, el, rec):
        if i == 1000:
            rec["io"]["p95"] = soak72.P95_MAX_MS + 1

    code, s = run_case("p95", build(4.0, mutate=p95_high), hours=4.0)
    check("p95_fail", code == 1 and state_of(s, "net_health") == "fail",
          "code=%s %s" % (code, detail_of(s, "net_health")))

    # --- 19. 200 秒旧帧被当成在线：io.src 冻结 → FAIL（假在线）
    def src_freeze(i, el, rec):
        if i >= 300:
            rec["io"]["src"] = SRC0 + 300 * 10

    code, s = run_case("srcfreeze", build(4.0, mutate=src_freeze), hours=4.0)
    check("source_frozen_fail", code == 1 and state_of(s, "source_progress") == "fail",
          "code=%s %s" % (code, detail_of(s, "source_progress")))

    # --- 20. io.src 缺失（-1）→ FAIL
    def src_missing(i, el, rec):
        if i >= 300:
            rec["io"]["src"] = -1

    code, s = run_case("srcmissing", build(4.0, mutate=src_missing), hours=4.0)
    check("source_missing_fail", code == 1 and state_of(s, "source_progress") == "fail",
          "code=%s %s" % (code, detail_of(s, "source_progress")))

    # --- 21. io.age 长时间不新鲜 → FAIL
    def stale(i, el, rec):
        if i >= 300:
            rec["io"]["age"] = int(soak72.IO_AGE_MAX_S) + 1

    code, s = run_case("stale", build(4.0, mutate=stale), hours=4.0)
    check("io_age_fail", code == 1 and state_of(s, "net_health") == "fail",
          "code=%s %s" % (code, detail_of(s, "net_health")))

    # --- 21b. 全程从未成功接收（io.age/io.src 恒 -1）→ 「测不到」，判 INCOMPLETE 而不是 FAIL
    def no_link(i, el, rec):
        rec["io"].update({"age": -1, "src": -1, "ok": 0})

    code, s = run_case("nolink", build(4.0, mutate=no_link), hours=4.0)
    check("never_connected_incomplete",
          code == 2 and s["verdict"] == "INCOMPLETE"
          and state_of(s, "net_health") == "incomplete"
          and state_of(s, "source_progress") == "incomplete",
          "code=%s verdict=%s net=%s src=%s" % (code, s["verdict"],
                                                state_of(s, "net_health"),
                                                state_of(s, "source_progress")))

    # --- 21c. 同一根因的另一侧：只是"部分变旧"（能判）就要 FAIL，不能被 INCOMPLETE 吞掉
    # 注意必须落在预热期（7200s / i=720）之后，否则这条样本根本不参与联网判据。
    def briefly_stale(i, el, rec):
        if 900 <= i < 921:
            rec["io"]["age"] = -1

    code, s = run_case("partstale", build(4.0, mutate=briefly_stale), hours=4.0)
    check("partial_stale_fail", code == 1 and state_of(s, "net_health") == "fail",
          "code=%s %s" % (code, detail_of(s, "net_health")))

    # --- 22. 故障窗口：窗口内联网指标豁免，窗口结束后 10s 就恢复 → PASS
    frozen_ok = int(WINDOW_A / 10.0)
    frozen_src = SRC0 + int(WINDOW_A / 10.0) * 10

    def in_window(i, el, rec):
        if WINDOW_A <= el < WINDOW_B:
            rec["io"].update({"age": 999, "p95": 9000, "ok": frozen_ok, "src": frozen_src})

    code, s = run_case("winok", build(72.0, mutate=in_window), hours=72.0,
                       windows=[{"start_s": WINDOW_A, "end_s": WINDOW_B}])
    check("fault_window_exempt_pass",
          code == 0 and s["verdict"] == "PASS" and state_of(s, "fault_recovery") == "pass",
          "code=%s verdict=%s recovery=%s net=%s" % (code, s["verdict"],
                                                     detail_of(s, "fault_recovery"),
                                                     detail_of(s, "net_health")))

    # --- 23. 故障窗口结束后 60s 内没有任何推进 → FAIL
    W3A, W3B = 3 * 3600.0, 3 * 3600.0 + 60.0
    w3_ok, w3_src = int(W3A / 10.0), SRC0 + int(W3A / 10.0) * 10

    def never_recovers(i, el, rec):
        if el >= W3A:
            rec["io"].update({"ok": w3_ok, "src": w3_src})

    code, s = run_case("winbad", build(4.0, mutate=never_recovers), hours=4.0,
                       windows=[{"start_s": W3A, "end_s": W3B}])
    check("fault_not_recovered_fail", code == 1 and state_of(s, "fault_recovery") == "fail",
          "code=%s %s" % (code, detail_of(s, "fault_recovery")))

    # --- 24. 窗口内不豁免硬判据：窗口里栈击穿照样 FAIL
    W4A, W4B = 3000.0, 3600.0

    def stack_in_window(i, el, rec):
        if W4A <= el < W4B:
            rec["stk"]["lvgl"] = soak72.STACK_MIN_BYTES - 1

    code, s = run_case("winstack", build(4.0, mutate=stack_in_window), hours=4.0,
                       windows=[{"start_s": W4A, "end_s": W4B}])
    check("window_does_not_hide_stack", code == 1 and state_of(s, "stack_floor") == "fail",
          "code=%s %s" % (code, detail_of(s, "stack_floor")))

    # --- 25. 故障窗口文件非法 → 退出码 3（三种非法形态）
    codes = []
    for bad in ([{"start_s": 100, "end_s": 50}],
                [{"start_s": 0, "end_s": 100}, {"start_s": 50, "end_s": 200}],
                {"start_s": 0, "end_s": 100}):
        d = os.path.join(TMP, "badwin%d" % len(codes))
        os.makedirs(d, exist_ok=True)
        write_metrics(os.path.join(d, "metrics.jsonl"), build(1.0))
        wp = os.path.join(d, "w.json")
        with open(wp, "w", encoding="utf-8") as fh:
            json.dump(bad, fh)
        args = argparse.Namespace(**{"input": os.path.join(d, "metrics.jsonl"), "hours": 1.0,
                                     "elf": ELF, "output": os.path.join(d, "rev"),
                                     "fault_windows": wp})
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            codes.append(soak72.analyze_main(args))
    check("invalid_fault_windows_exit3", codes == [3, 3, 3], "退出码 %s" % codes)

    # --- 26. fw 与本地 ELF 不一致 → FAIL（跑的不是这次构建）
    code, s = run_case("fwbad", build(4.0, fw=BAD_SHA), hours=4.0)
    check("firmware_mismatch_fail", code == 1 and state_of(s, "firmware_match") == "fail",
          "code=%s %s" % (code, detail_of(s, "firmware_match")))

    # --- 27. metrics.jsonl 破损 → 退出码 3，但仍然要落一份 summary.json
    code, s = run_case("brokenjson", build(1.0), hours=1.0,
                       raw_metrics='{"t": 1.0, "rec": {"n": 1}}\n{"t": 2.0, "rec":\n')
    check("broken_metrics_exit3", code == 3 and s["verdict"] == "INPUT_ERROR",
          "code=%s verdict=%s" % (code, s.get("verdict")))

    # --- 28. 记录里混进未知键（令牌之类）→ 退出码 3，且令牌不得出现在 summary 里
    secret = "SECRET-TOKEN-1f4a"

    def token_line():
        samples = build(1.0)
        samples[5]["rec"]["token"] = secret
        return json.dumps(samples[5]) + "\n"

    code, s = run_case("privacyrec", [], hours=1.0, raw_metrics=token_line())
    with open(os.path.join(TMP, "privacyrec", "review", "summary.json"), encoding="utf-8") as fh:
        summary_text = fh.read()
    check("privacy_extra_key_exit3", code == 3 and secret not in summary_text,
          "code=%s 令牌泄漏=%s" % (code, secret in summary_text))

    # --- 28b. 脏输入：空文件 / 只有半行 → 必须落 summary 并判 INCOMPLETE，不能抛栈
    code, s = run_case("emptyfile", [], hours=4.0, raw_metrics="")
    check("empty_metrics_incomplete", code == 2 and s["verdict"] == "INCOMPLETE",
          "code=%s verdict=%s" % (code, s.get("verdict")))
    code, s = run_case("halfline", [], hours=4.0,
                       raw_metrics='{"t": 1.0, "rec": {"n": 1, "u')
    check("truncated_tail_incomplete",
          code == 2 and s["verdict"] == "INCOMPLETE"
          and (s.get("metrics_load") or {}).get("malformed_tail") == 1,
          "code=%s verdict=%s load=%s" % (code, s.get("verdict"), s.get("metrics_load")))

    # --- 28c. 时间戳乱序：小幅调换仍可评价（容忍），大幅回退 = 主机时钟被回拨 → INCOMPLETE
    swapped = build(4.0)
    swapped[500]["t"], swapped[501]["t"] = swapped[501]["t"], swapped[500]["t"]
    code, s = run_case("swap", swapped, hours=4.0)
    check("timestamp_swap_tolerated",
          code == 0 and (s.get("metrics_load") or {}).get("out_of_order") == 1,
          "code=%s load=%s" % (code, s.get("metrics_load")))

    rolled = build(4.0)
    for i in range(401, len(rolled)):
        rolled[i]["t"] -= 7200.0
    code, s = run_case("clockroll", rolled, hours=4.0)
    check("clock_rollback_incomplete",
          code == 2 and s["verdict"] == "INCOMPLETE" and state_of(s, "evidence") == "incomplete",
          "code=%s verdict=%s %s" % (code, s.get("verdict"), detail_of(s, "evidence")))

    # --- 29. 解析纯函数：IDF 前缀 / 截断 / 纯文本 / 缺字段 / 类型不对
    good = sample_lines()[0]
    parsed = soak72.parse_line(good)
    bad_lines = [
        'I (1) main: FNOS_DIAG {"n":1,"up":10000,"hz":10,"fw":"%s","rst":1,"ev":[]' % ELF_SHA,
        "I (2) main: HTTP client: connected",
        'I (3) boot: FNOS_DIAG not-json',
        "",
    ]
    check("parse_line_marker_in_prefix",
          parsed is not None and parsed["n"] == 1 and parsed["io"]["src"] == SRC0,
          "解析结果 %s" % (parsed,))
    check("parse_line_rejects_malformed",
          all(soak72.parse_line(x) is None for x in bad_lines),
          "这些行必须全部返回 None：%s" % [soak72.parse_line(x) for x in bad_lines])

    # --- 30. 清洗函数拒绝越界记录（多键/类型错/缺键），接受合法记录
    ok_rec = soak72.parse_line(good)
    mutations = []
    extra = json.loads(json.dumps(ok_rec))
    extra["ssid"] = "home-wifi"
    mutations.append(extra)
    extra2 = json.loads(json.dumps(ok_rec))
    extra2["io"]["token"] = "SECRET-TOKEN"
    mutations.append(extra2)
    wrong_type = json.loads(json.dumps(ok_rec))
    wrong_type["n"] = "1"
    mutations.append(wrong_type)
    float_up = json.loads(json.dumps(ok_rec))
    float_up["up"] = 10000.0
    mutations.append(float_up)
    bool_rst = json.loads(json.dumps(ok_rec))
    bool_rst["rst"] = True
    mutations.append(bool_rst)
    missing = json.loads(json.dumps(ok_rec))
    del missing["rst"]
    mutations.append(missing)
    wrong_ev = json.loads(json.dumps(ok_rec))
    wrong_ev["ev"] = [1]
    mutations.append(wrong_ev)
    check("sanitize_accepts_valid", soak72.sanitize(ok_rec) == ok_rec, "合法记录被改动或拒绝")
    rejected = [soak72.sanitize(m) for m in mutations]
    check("sanitize_rejects_foreign_keys", all(r is None for r in rejected),
          "应被拒绝的 %d 条里放过了 %d 条" % (len(rejected), sum(1 for r in rejected if r is not None)))

    # --- 31. argv 脱敏：产物里不留家目录/串口路径
    argv = soak72.sanitized_argv(["soak72.py", "capture", "--port=/dev/cu.usbmodem1101",
                                  "--input", "/Users/someone/secret/metrics.jsonl",
                                  "--hours", "72"])
    flattened = " ".join(argv)
    check("argv_anonymized",
          "/Users" not in flattened and "usbmodem" not in flattened and "<port>" in flattened
          and "<input>" in flattened, "脱敏后 %s" % argv)

    # --- 32. 采集：只读、只落数值、结束标记 complete=true、退出码 0
    chunks = [(sample_lines()[0] + "\n" + 'I (2) main: FNOS_DIAG {"n":2,"up":200' + "\n"
               + "I (3) main: some plain log line\n" + sample_lines()[1] + "\n").encode()]
    code, cap, d, opens = capture_case("capok", chunks)
    metrics_path = os.path.join(d, "metrics.jsonl")
    with open(metrics_path, encoding="utf-8") as fh:
        lines = [json.loads(x) for x in fh if x.strip()]
    check("capture_exit0_complete",
          code == 0 and cap["complete"] is True and cap["sample_count"] == 2,
          "code=%s cap=%s" % (code, {k: cap.get(k) for k in ("complete", "sample_count", "parse_drop")}))
    check("capture_counts_malformed",
          cap["parse_drop"] == 2 and cap["parse_drop_run_max"] == 2,
          "parse_drop=%s run_max=%s" % (cap["parse_drop"], cap["parse_drop_run_max"]))
    check("capture_numeric_only",
          len(lines) == 2 and all(set(x) == {"t", "rec"} and x["rec"]["fw"] == ELF_SHA for x in lines),
          "metrics.jsonl 里有 %d 行，首行键 %s" % (len(lines), sorted(lines[0]) if lines else None))
    with open(metrics_path, encoding="utf-8") as fh:
        raw_metrics_text = fh.read()
    check("capture_drops_raw_text",
          "plain log line" not in raw_metrics_text and "FNOS_DIAG" not in raw_metrics_text
          and "I (2) main" not in raw_metrics_text,
          "metrics.jsonl 里混进了原始串口文本")
    check("capture_port_anonymized",
          cap["port"] == "serial" and cap["elf_sha256"] == ELF_SHA
          and not privacy_scan(os.path.join(d, "capture.json")),
          "port=%s 残留=%s" % (cap["port"], privacy_scan(os.path.join(d, "capture.json"))))
    check("capture_argv_anonymized",
          any("<port>" in a for a in cap["argv"]) and "<elf>" in cap["argv"],
          "argv=%s" % cap["argv"])

    # --- 33. 掉线：记一条 error 就收工，不重开串口、不留 complete=true、不泄漏设备路径
    code, cap, d, opens = capture_case("capfail", [(sample_lines()[0] + "\n").encode()], raise_after=1)
    errs = cap.get("errors") or []
    check("capture_disconnect_no_retry",
          code == 1 and cap["complete"] is False and errs and opens == 1
          and errs[0]["event"] == "serial_read_failed" and errs[0]["exc"] == "OSError",
          "code=%s complete=%s 开端口次数=%d errors=%s" % (code, cap["complete"], opens, errs))
    check("capture_fail_no_path_leak", not privacy_scan(os.path.join(d, "capture.json")),
          "残留=%s" % privacy_scan(os.path.join(d, "capture.json")))

    # --- 34. 缺端口 / ELF 不存在 → 退出码 3（不猜、不降级）
    saved = os.environ.pop(soak72.ENV_PORT, None)
    try:
        d = os.path.join(TMP, "noport")
        args = argparse.Namespace(port=None, elf=ELF, output=d, hours=1.0, fault_windows=None)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            no_port = soak72.capture_main(args, ["soak72.py", "capture"])
            args2 = argparse.Namespace(port="/dev/cu.usbmodem1101",
                                       elf=os.path.join(TMP, " missing.elf"), output=d,
                                       hours=1.0, fault_windows=None)
            no_elf = soak72.capture_main(args2, ["soak72.py", "capture"])
    finally:
        if saved is not None:
            os.environ[soak72.ENV_PORT] = saved
    check("capture_input_errors_exit3", no_port == 3 and no_elf == 3,
          "缺端口=%s 缺 ELF=%s" % (no_port, no_elf))

    # --- 35. analyze 缺输入文件 / 缺 ELF → 退出码 3
    d = os.path.join(TMP, "noinput")
    os.makedirs(d, exist_ok=True)
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        c1 = soak72.analyze_main(argparse.Namespace(**{"input": os.path.join(d, "nope.jsonl"),
                                                       "hours": 1.0, "elf": ELF,
                                                       "output": d, "fault_windows": None}))
        c2 = soak72.analyze_main(argparse.Namespace(**{"input": os.path.join(d, "nope.jsonl"),
                                                       "hours": 1.0,
                                                       "elf": os.path.join(d, "nope.elf"),
                                                       "output": d, "fault_windows": None}))
    check("analyze_input_errors_exit3", c1 == 3 and c2 == 3, "缺 metrics=%s 缺 ELF=%s" % (c1, c2))

    # --- 36. summary.json 必须写明"不得相加"的语义与逐条实测数值
    _, s = run_case("report", build(4.0), hours=4.0)
    notes = "".join(s["notes"])
    check("summary_notes_and_measurements",
          "不得相加" in notes and all("measured" in c for c in s["checks"])
          and any(c["name"] == "heap_floor" and "pfree" in c["measured"] for c in s["checks"]),
          "notes=%d 条，checks=%d 条" % (len(s["notes"]), len(s["checks"])))


if __name__ == "__main__":
    try:
        main()
    finally:
        shutil.rmtree(TMP, ignore_errors=True)
    print()
    if FAILED:
        print("FAIL %d/%d 项（下面的用例没抓住它该抓的东西）" % (len(FAILED), COUNT))
        for name, detail in FAILED:
            print("  ✗ %s  %s" % (name, detail))
        sys.exit(1)
    print("PASS %d 项 soak72 判据回归全部通过" % COUNT)
    sys.exit(0)
