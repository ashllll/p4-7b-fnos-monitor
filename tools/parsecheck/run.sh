#!/bin/bash
# 把一份**满载**的 /api/v1/status 喂给固件里那个 parse_status()，看板子吃不吃得下。
#
# 为什么值得单独做：`contract_check.py` 只核对"字段名在不在"，没有任何检查验证
# 板子**能不能解析满载的那一帧**。而这一层是固件里跑得最勤的一段代码（每秒一次），
# 却从来没在设备之外执行过——主机预览把 fnos_data 整个换成了替身。
#
# 做法与 certcheck 一致：从 fnos_data.c / fnos_snapshot.c 里**原样抠出**解析那一串
# 函数（解析层 1.2.5 起搬到了 fnos_snapshot.c，两个文件都要看）编译到主机
# （不手抄，抄了就等于测手抄的那份），cJSON 用工程里那一份。
set -u
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cd "$(cd "$(dirname "$0")/../.." && pwd)"

OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
python3 tools/parsecheck/extract.py components/fnos_monitor/fnos_data.c "$OUT/ps.c" || exit 1
# 满载样本由 python 生成（各段都到上限），见 make_payload.py 头部说明；
# 用多大的样本由下面的容量探针挑，这里先不生成。

CJ=managed_components/espressif__cjson/cJSON
cc -I build/config -I "$CJ" -I components/fnos_monitor -o "$OUT/ps" \
   tools/parsecheck/main.c "$OUT/ps.c" "$CJ/cJSON.c" 2>&1 | head -20
[ -x "$OUT/ps" ] || { echo "编译失败"; exit 1; }

# 历史回填：同样从没在设备外跑过，而且刚改过 ?n= 协议
python3 - "$OUT/full.json" "$OUT/hist.json" <<'PY'
import json, sys
# 300 行、每行 1 s 间隔（ts 递增 1），这样 hist_span_s 应当正好是 299
# **必须超过 FNOS_HIST_MAX（600）**，否则环形缓冲根本不绕圈：
# 第一版用 300 行，把 `since_seq % FNOS_HIST_MAX` 写成 `since_seq` 照样全绿 ——
# 300 < 600，取模和不取模完全一样。这是本项目第五次踩同一个坑：
# **要验"上限"的逻辑，数据必须先超过那个上限。**
rows = [[1791200000 + i, 30.0 + i % 50, 60.0, 1000.0 + i, 200.0 + i] for i in range(900)]
json.dump({"cols": ["ts", "cpu", "mem", "rx_kbs", "tx_kbs"], "rows": rows, "n": len(rows)},
          open(sys.argv[2], "w"), ensure_ascii=False, separators=(",", ":"))
# 期望值也在这里算，别在 C 里写死——生成器和期望必须同源
open(sys.argv[2] + ".want", "w").write("%d %d" % (rows[-1][0] - rows[0][0],
                                                  (rows[-1][0] - rows[0][0]) * 10 // (len(rows) - 1)))
# 期望值也在这里算：环形缓冲读回来之后第 0 行该是什么、一共有多少行
open(sys.argv[2] + ".want2", "w").write("%d %.2f %.2f" % (
    min(len(rows), 600), rows[0][1] if len(rows) <= 600 else rows[-600][1], 
    rows[0][3] if len(rows) <= 600 else rows[-600][3]))
print("历史样本：900 行（超过环形缓冲 600），间隔各 1 s")
PY

# 期望值按**空格分开**读进来再逐个数传参：`"$(cat want)"` 会把 "899 10" 当成
# 一个 argv，主程序只看到 argc=4 ⇒ gap 期望变 -1 ⇒ 每一档都判 BADHIST。
# 这个引号陷阱让探针对**所有**填充度都判失败，看着像"板子连 62 KB 都吃不下"。
read -r WANT_SPAN WANT_GAP < "$OUT/hist.json.want"
read -r WANT_N WANT_CPU0 WANT_RX0 < "$OUT/hist.json.want2"
run_ps() { "$OUT/ps" "$OUT/full.json" "$OUT/hist.json" \
             "$WANT_SPAN" "$WANT_GAP" "$WANT_N" "$WANT_CPU0" "$WANT_RX0"; }

# 容量探针：从高到低试几档填充度，量出"板子能解析的最大帧"。
# 为什么探而不是写死一档：payload 字节与 arena 不是 1:1 —— arena 还要装结构体
# 向量和每串的账头，所以 1 MiB 的预算装不下 1 MiB 的 payload（实测 0.80 档、
# 995 KB 的帧就是 "data capacity"）。写死一档要么红得没道理，要么松得测不出回归。
FLOOR=131072                     # 至少要能吃下 128 KB 的帧（历史上出事的那帧只有 8.3 KB）
CEIL=""; CEILSZ=0; FIRSTFAIL=""
for F in 0.80 0.70 0.60 0.50 0.45 0.40 0.35 0.30 0.25 0.20 0.15 0.10 0.05; do
  PARSE_FILL=$F python3 tools/parsecheck/make_payload.py > "$OUT/full.json" 2>/dev/null || exit 1
  if run_ps >/dev/null 2>&1; then
    CEIL=$F; CEILSZ=$(wc -c < "$OUT/full.json" | tr -d ' '); break
  fi
  [ -n "$FIRSTFAIL" ] || FIRSTFAIL=$F
done
if [ -z "$CEIL" ]; then
  echo "✗ 容量探针：连 arena 预算 5% 的帧都解析不了 —— 这是解析回归，不是样本太大"
  PARSE_FILL=0.80 python3 tools/parsecheck/make_payload.py > "$OUT/full.json" || exit 1
  run_ps
  exit 1
fi
[ "$CEILSZ" -ge "$FLOOR" ] || {
  echo "✗ 容量探针：板子只吃下 ${CEILSZ} 字节，低于下限 ${FLOOR}"
  PARSE_FILL=$CEIL python3 tools/parsecheck/make_payload.py > "$OUT/full.json" || exit 1
  run_ps
  exit 1
}
PARSE_FILL=$CEIL python3 tools/parsecheck/make_payload.py > "$OUT/full.json"   # 重放一次，口径走 stderr
run_ps || { echo "✗ parse_status() 或 parse_history() 失败"; exit 1; }
echo "✓ 满载帧（${CEILSZ} 字节 = arena 预算的 ${CEIL} 档）能被 parse_status() 解析"
[ -z "$FIRSTFAIL" ] || echo "  （${FIRSTFAIL} 档解析不了：payload 字节与 arena 预算不是 1:1 —— arena 还要装结构体向量和每串账头；NAS 侧 DEFAULT_LIMITS={} 不截断，真机把帧撑到这个量级就会在板端解析失败。）"
