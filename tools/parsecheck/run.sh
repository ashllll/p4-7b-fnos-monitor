#!/bin/bash
# 把一份**满载**的 /api/v1/status 喂给固件里那个 parse_status()，看板子吃不吃得下。
#
# 为什么值得单独做：`contract_check.py` 只核对"字段名在不在"，没有任何检查验证
# 板子**能不能解析满载的那一帧**。而这一层是固件里跑得最勤的一段代码（每秒一次），
# 却从来没在设备之外执行过——主机预览把 fnos_data 整个换成了替身。
#
# 做法与 certcheck 一致：从 fnos_data.c 里**原样抠出**那五个函数编译到主机
# （不手抄，抄了就等于测手抄的那份），cJSON 用工程里那一份。
set -u
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cd "$(cd "$(dirname "$0")/../.." && pwd)"

OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
python3 tools/parsecheck/extract.py components/fnos_monitor/fnos_data.c "$OUT/ps.c" || exit 1
# 满载样本由 python 生成（各段都到上限），见 make_payload.py 头部说明
python3 tools/parsecheck/make_payload.py > "$OUT/full.json" || exit 1
python3 - "$OUT/full.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("满载 payload：%d 字节（各段都到上限）" % len(json.dumps(d, ensure_ascii=False,
                                                          separators=(",", ":")).encode()))
PY

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

"$OUT/ps" "$OUT/full.json" "$OUT/hist.json" $(cat "$OUT/hist.json.want") \
  $(cat "$OUT/hist.json.want2") \
  || { echo "✗ parse_status() 或 parse_history() 失败"; exit 1; }
SIZE=$(wc -c < "$OUT/full.json" | tr -d ' ')
echo "✓ 满载帧（${SIZE} 字节）能被 parse_status() 解析"
