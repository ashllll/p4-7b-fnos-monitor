#!/usr/bin/env bash
# 四级证据一键验收（见 docs/ui-v12-lvgl-native.md §7）：
#   L1 主机预览（版面/字库/越界/压叠）+ 自适应池几何
#   L2 固件构建 + 已登记入口静态栈深
#   L3 实机（--flash）：烧录 → 串口 30s 崩溃/复位扫描 + 堆
#   L3 拍屏（--photo）：capture-board 手机取景
#
# 用法：bash tools/verify_all.sh [--flash] [--photo] [--port /dev/cu.usbmodemXXXX]
# 任何一级失败即退出非零，并在最后打印 PASS/FAIL 汇总。
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
PROJ="$PWD"
OUT="${VERIFY_OUT:-/tmp/fnos-verify}"
FLASH=0; PHOTO=0; PORT="${FNOS_SERIAL_PORT:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --flash) FLASH=1 ;;
    --photo) PHOTO=1 ;;
    --port)
      [ $# -ge 2 ] && [ -n "$2" ] || { echo "--port requires a value" >&2; exit 2; }
      shift; PORT="$1" ;;
    *) echo "未知参数：$1"; exit 2 ;;
  esac
  shift
done

if [ "$FLASH" = 1 ] && [ -z "$PORT" ]; then
  echo "--flash requires --port or FNOS_SERIAL_PORT for the identified board" >&2
  exit 2
fi
if [ -n "${FNOS_IDF_ENV:-}" ]; then
  . "$FNOS_IDF_ENV" || exit 2
fi
# PREVIEW_* 会在 preview.c 里切成"只渲染子集"的模式（审计 M36）：残留一个就退 0 而审计没跑。
# Clear every exported preview selector, including newly introduced ones.
for preview_var in $(compgen -e); do
  case "$preview_var" in PREVIEW_*) unset "$preview_var" ;; esac
done
ok=0; fail=0
step() { printf '\n=== %s ===\n' "$1"; }
pass() { printf '  [PASS] %s\n' "$1"; ok=$((ok+1)); }
bad()  { printf '  [FAIL] %s\n' "$1"; fail=$((fail+1)); }

# Preserve pre-existing paths: never recursively delete a caller-provided output directory.
prepare_output() {
  mkdir -p -- "$1" || return 1
  mktemp -d "$1/run.XXXXXX"
}
# This validates the file only; physical display judgement belongs to the caller.
capture_board_photo() {
  python3 - "$1" <<'PY'
import json, shutil, sys
from pathlib import Path
sys.path.insert(0, 'tools')
from page_shot import capture_photo
from photo_check import decode_png
out = Path(sys.argv[1])
info = capture_photo()
image = out / 'board-photo.png'
shutil.copyfile(info['image'], image)
decode_png(image)
with (out / 'photo.json').open('w', encoding='utf-8') as target:
    json.dump(info, target)
print('Capture copied and decoded; visual acceptance remains pending')
PY
}

OUT=$(prepare_output "$OUT") || { echo "Cannot create verification output directory" >&2; exit 2; }
echo "验收结果目录：$OUT"

# ── L1：主机预览 ───────────────────────────────────────────────────
step "L1 主机预览（真正的 UI 源码，不是原型）"
if DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build tools/preview/build -j8 >"$OUT/build.log" 2>&1; then
  pass "预览构建"
else
  bad "预览构建失败（见 $OUT/build.log）"; tail -20 "$OUT/build.log"; exit 1
fi
# 审计关键字与分诊只有一份实现（tools/preview/render_diagnose.sh，单测见 test_render_diagnosis.sh）：
# 越界 / 压叠 / 溢出 = 几何问题，missing glyph 才是字形缺口。以前这里自己写死一句
# "多为字形缺口" 就 exit，几何问题被指去补字库，而下面那段按关键字分诊的代码**永远走不到**（审计 M29）。
# 关键字模式也必须与 preview.c 真正打印的英文一致（审计 M29）：以前写的是中文词（只存在于注释里），
# 而 `overflow` 也不等于 `overlap` → child out of parent / inline metric overflow /
# text overflow 三类永远匹配不到。
RENDER_RC=0
mkdir -p "$OUT/png"
DEVELOPER_DIR=/Library/Developer/CommandLineTools PREVIEW_BARMAP="$OUT/png/barmap.txt" tools/preview/build/preview "$OUT/png" >"$OUT/preview.log" 2>&1 || RENDER_RC=$?
AUDIT_RC=0
DIAG=$(bash tools/preview/render_diagnose.sh "$RENDER_RC" "$OUT/preview.log") || AUDIT_RC=$?
if [ "$RENDER_RC" != "0" ]; then
  # 渲染都没成功，后面几步（PNG 转换 / 帧数 / 滚动条审计）没有意义，但**先把红在哪说清楚**
  bad "$DIAG"; exit 1
fi
if [ "$AUDIT_RC" = "0" ]; then
  pass "渲染全部状态"
  pass "几何审计（无越界/压叠/字形缺口）"
else
  # 渲染退出 0、但审计打了失败行：照样是硬失败，且按同一套关键字分诊
  bad "渲染完成，但审计有失败项 —— $DIAG"
fi
if python3 tools/preview/ppm2png.py "$OUT/png" >"$OUT/ppm2png.log" 2>&1; then
  pass "PPM→PNG"
else
  # 以前只有 `&& pass`：转换失败时报告里连一行都没有，整条门照样全绿（审计 M47）
  bad "PPM→PNG 转换失败（见 $OUT/ppm2png.log）"; tail -5 "$OUT/ppm2png.log"
fi
# 帧数也是门（审计 M36）：preview.c 见到 PREVIEW_MOTION / PREVIEW_NUMBERS / PREVIEW_CARDMAP /
# PREVIEW_PAIR_FIRST_ENTRY / PREVIEW_DATA_MOTION 任一就只渲子集然后 return 0。
FRAMES=$(find "$OUT/png" -maxdepth 1 -name '*.png' | wc -l | tr -d ' ')
if [ "$FRAMES" -lt 84 ]; then
  bad "只渲染了 $FRAMES 张（应 ≥84）：审计没跑全，别把这次结果当验收"
else
  pass "渲染帧数 $FRAMES"
fi
# 滚动条侵扰（审计 H14）：几何由被审计的二进制自己吐（PREVIEW_CARDMAP=1），判据见脚本头部。
if command -v node >/dev/null 2>&1; then
  if node tools/preview/scroll_gap.js "$OUT/png" >"$OUT/scroll_gap.log" 2>&1; then
    grep -E "合格|未判定|合计" "$OUT/scroll_gap.log" | tail -3 | sed 's/^/  /'
    pass "滚动条间隙审计"
  else
    bad "滚动条间隙审计不通过（见 $OUT/scroll_gap.log）"; tail -6 "$OUT/scroll_gap.log"
  fi
else
  bad "没有 node，滚动条间隙审计没跑（H14 的自动化证据缺失）"
fi
# snapshot_check 是快照 ownership/并发/预算的唯一自动化验收，必须真执行（审计 M16）
if [ -x tools/preview/build/snapshot_check ]; then
  if tools/preview/build/snapshot_check >"$OUT/snapshot_check.log" 2>&1; then
    pass "快照 ownership/并发/预算（snapshot_check）"
  else
    bad "snapshot_check 失败（见 $OUT/snapshot_check.log）"; tail -10 "$OUT/snapshot_check.log"
  fi
else
  bad "没有 tools/preview/build/snapshot_check，快照验收没跑"
fi

grep -h "见过的构件" "$OUT/preview.log" | sed 's/^/  /'
echo "  验收图：$OUT/png/（$(ls "$OUT/png" | wc -l | tr -d ' ') 张）"

# ── L1.5：NAS 侧契约/采集端检查器 ──────────────────────────────────
# 以前这些脚本只有 test_lifecycle.sh 里跑过 contract/api 两个，其余的从没进过统一入口，
# 于是 H5（ZFS 契约）、H12（TLS 接受阻塞）、M40（POST 字段名未覆盖）、M58（fuse.* 前缀）
# 的回归就没人自动跑。
step "L1.5 NAS 侧契约与采集端检查"
for checker in contract_check api_check collector_check tls_accept_check perm_check wizard_check test_server_safety; do
  if python3 "nas/fpk/$checker.py" >"$OUT/$checker.log" 2>&1; then
    pass "nas/fpk/$checker.py"
  else
    bad "nas/fpk/$checker.py 失败（见 $OUT/$checker.log）"; tail -5 "$OUT/$checker.log"
  fi
done
if [ -n "${FNOS_BOARD_SIM_HOST:-}" ] && [ -n "${FNOS_BOARD_SIM_PORT:-}" ]; then
  if python3 nas/fpk/board_sim.py --host "$FNOS_BOARD_SIM_HOST" --port "$FNOS_BOARD_SIM_PORT" >"$OUT/board_sim.log" 2>&1; then
    pass "板子模拟器（对着真 NAS）"
  else
    bad "板子模拟器失败（见 $OUT/board_sim.log）"; tail -8 "$OUT/board_sim.log"
  fi
else
  echo "  [SKIP] 板子模拟器：没设 FNOS_BOARD_SIM_HOST/PORT（本机没有真 NAS，不算通过）"
fi

# ── L1.6：本轮修复的回归 ──────────────────────────────────────────
# 三条都是"假绿"类缺陷的负向回归：停止失败时删记录/写配置（M21/M25/M26）、安装令牌
# fail-closed 路径（M31）、预览输出目录越界与分支变量泄漏（M36/M57）。全部在临时目录里
# 用桩件跑（桩 kill / 桩 systemctl / 派生的预览副本），不碰真 NAS、不碰真服务。
step "L1.6 本轮修复回归"
if python3 nas/fpk/test_lifecycle_failures.py >"$OUT/lifecycle_failures.log" 2>&1; then
  pass "PID 写失败及卸载收尾失败（无 NAS）"
else
  bad "生命周期故障回归失败（见 $OUT/lifecycle_failures.log）"
fi
if python3 nas/fpk/test_validation_gates.py >"$OUT/validation_gates.log" 2>&1; then
  pass "API 与向导规则假绿负控（无 NAS）"
else
  bad "API/向导门回归失败（见 $OUT/validation_gates.log）"
fi
if python3 -W error::ResourceWarning nas/fpk/test_tool_cleanup.py >"$OUT/tool_cleanup.log" 2>&1; then
  pass "检查器父/子进程与权限临时文件故障收尾"
else
  bad "检查器故障收尾回归失败（见 $OUT/tool_cleanup.log）"
fi
if python3 tools/test_pair_state.py >"$OUT/pair_state.log" 2>&1; then
  pass "配对状态边界与进行中动作隔离（无网络）"
else
  bad "配对状态回归失败（见 $OUT/pair_state.log）"
fi
if python3 tools/test_pair_nvs.py >"$OUT/pair_nvs.log" 2>&1; then
  pass "配对存储读取故障拒绝降级（无设备）"
else
  bad "配对存储回归失败（见 $OUT/pair_nvs.log）"
fi
if python3 tools/test_pair_nvs_transaction.py >"$OUT/pair_nvs_transaction.log" 2>&1; then
  pass "配对存储屏障、部分失败与模拟重启（无设备）"
else
  bad "配对存储事务回归失败（见 $OUT/pair_nvs_transaction.log）"
fi
if python3 tools/test_serial_capture.py >"$OUT/serial_capture.log" 2>&1; then
  pass "串口采集复位条件与故障收尾（模拟串口）"
else
  bad "串口采集回归失败（见 $OUT/serial_capture.log）"
fi
if python3 -W error::ResourceWarning nas/fpk/test_ssh_scripts.py >"$OUT/ssh_scripts.log" 2>&1; then
  pass "SSH 主机信任、中转清理与远程安装失败（纯桩）"
else
  bad "SSH/远程安装回归失败（见 $OUT/ssh_scripts.log）"
fi
if python3 tools/test_pair_commit.py >"$OUT/pair_commit.log" 2>&1; then
  pass "配对持久化与发布锁失败（无设备）"
else
  bad "配对提交回归失败（见 $OUT/pair_commit.log）"
fi
if python3 tools/test_tls_probe_guard.py >"$OUT/tls_probe_guard.log" 2>&1; then
  pass "TLS 探针重入与独立诊断缓冲（无网络）"
else
  bad "TLS 探针回归失败（见 $OUT/tls_probe_guard.log）"
fi
if python3 tools/test_ui_requests.py >"$OUT/ui_requests.log" 2>&1; then
  pass "UI 原子请求与刷新定时器重试（无设备）"
else
  bad "UI 请求回归失败（见 $OUT/ui_requests.log）"
fi
if python3 tools/test_main_timers.py >"$OUT/main_timers.log" 2>&1; then
  pass "启动定时器失败与资源回收（无设备）"
else
  bad "启动定时器回归失败（见 $OUT/main_timers.log）"
fi
if python3 -W error::ResourceWarning tools/test_photo_check.py >"$OUT/photo_check.log" 2>&1; then
  pass "照片解码预算与坏图拒绝（匿名PNG）"
else
  bad "照片检查回归失败（见 $OUT/photo_check.log）"
fi
if python3 tools/test_history_merge.py >"$OUT/history_merge.log" 2>&1; then
  pass "历史与实时顺序、游标失效及端点重置（无设备）"
else
  bad "历史合并回归失败（见 $OUT/history_merge.log）"
fi
if python3 tools/test_history.py >"$OUT/history.log" 2>&1; then
  pass "历史回填时间边界（无设备）"
else
  bad "历史回填时间回归失败（见 $OUT/history.log）"
fi
if python3 tools/test_evidence_gates.py >"$OUT/evidence_gates.log" 2>&1; then
  pass "新鲜度证据缺失拒绝（匿名临时文件）"
else
  bad "新鲜度证据回归失败（见 $OUT/evidence_gates.log）"
fi
if python3 tools/test_cert_info.py >"$OUT/cert_info.log" 2>&1; then
  pass "证书辅助信息真实 mbedTLS 主机解析（无 NAS）"
else
  bad "证书辅助信息回归失败（见 $OUT/cert_info.log）"
fi
if python3 tools/test_wifi_store.py >"$OUT/wifi_store.log" 2>&1; then
  pass "Wi-Fi 存储屏障、读取拒绝与模拟重启（无设备）"
else
  bad "Wi-Fi 存储回归失败（见 $OUT/wifi_store.log）"
fi
if python3 tools/test_wifi_cli.py >"$OUT/wifi_cli.log" 2>&1; then
  pass "串口命令边界、数字溢出与口令行清理（无设备）"
else
  bad "串口命令回归失败（见 $OUT/wifi_cli.log）"
fi
if python3 tools/test_pair_diag.py >"$OUT/pair_diag.log" 2>&1; then
  pass "配对诊断缓冲任务边界（无设备）"
else
  bad "配对诊断缓冲回归失败（见 $OUT/pair_diag.log）"
fi
if python3 tools/test_pair_snapshot.py >"$OUT/pair_snapshot.log" 2>&1; then
  pass "配对配置同锁快照与不可用拒绝（无设备）"
else
  bad "配对快照回归失败（见 $OUT/pair_snapshot.log）"
fi
if python3 nas/fpk/test_upgrade_safety.py >"$OUT/upgrade_safety.log" 2>&1; then
  pass "升级回退点、恢复标志与故障重试（无 NAS）"
else
  bad "升级故障回归失败（见 $OUT/upgrade_safety.log）"
fi
if python3 nas/fpk/test_port_preflight.py >"$OUT/port_preflight.log" 2>&1; then
  pass "全地址监听预检与未判定拒绝（无 NAS）"
else
  bad "端口预检回归失败（见 $OUT/port_preflight.log）"
fi
if python3 -W error::ResourceWarning tools/test_report_entry.py >"$OUT/report_entry.log" 2>&1; then
  pass "报告入口失败传播与未完成状态（纯桩）"
else
  bad "报告入口回归失败（见 $OUT/report_entry.log）"
fi
if python3 tools/test_host_gate_failures.py >"$OUT/host_gate_failures.log" 2>&1; then
  pass "空原因集合与缺预览配置负控"
else
  bad "主机门负控失败（见 $OUT/host_gate_failures.log）"
fi
for checker in test_gate_failures test_manifest_failures; do
  if python3 -W error::ResourceWarning "tools/$checker.py" >"$OUT/$checker.log" 2>&1; then
    pass "tools/$checker.py"
  else
    bad "回归失败（见 $OUT/$checker.log）"
  fi
done
if python3 tools/test_perf_samples.py >"$OUT/perf_samples.log" 2>&1; then
  pass "性能样本完整性（无设备）"
else
  bad "性能样本回归失败（见 $OUT/perf_samples.log）"
fi
if python3 tools/test_stack_frames.py >"$OUT/stack_frames.log" 2>&1; then
  pass "大小栈帧与未知调整负控"
else
  bad "栈帧解析回归失败（见 $OUT/stack_frames.log）"
fi
if python3 tools/test_wifi_submission.py >"$OUT/wifi_submission.log" 2>&1; then
  pass "配网提交代次、迟到事件与 RPC 失败"
else
  bad "配网事件回归失败（见 $OUT/wifi_submission.log）"
fi
if python3 tools/test_page_shot.py >"$OUT/page_shot_mock.log" 2>&1; then
  pass "拍屏错误处理（模拟，无设备）"
else
  bad "拍屏错误处理回归失败（见 $OUT/page_shot_mock.log）"
fi
if bash nas/fpk/test_process_lifecycle.sh >"$OUT/proc_lifecycle.log" 2>&1; then
  pass "停止失败不删记录、不写配置（M21/M25/M26）"
else
  bad "进程生命周期回归失败（见 $OUT/proc_lifecycle.log）"; tail -8 "$OUT/proc_lifecycle.log"
fi
if python3 nas/fpk/test_install_token.py >"$OUT/install_token.log" 2>&1; then
  pass "安装令牌 fail-closed：首次/升级/故障路径（M31）"
else
  bad "安装令牌回归失败（见 $OUT/install_token.log）"; tail -8 "$OUT/install_token.log"
fi
if bash tools/preview/test_release_asserts.sh >"$OUT/preview_asserts.log" 2>&1; then
  pass "Release 断言与坏夹具拒绝（无设备）"
else
  bad "Release 断言回归失败或未完成（见 $OUT/preview_asserts.log）"
fi
if bash tools/preview/test_run_guard.sh >"$OUT/preview_guard.log" 2>&1; then
  pass "预览输出目录守卫 + 分支变量按阶段隔离（M36/M57）"
else
  bad "预览守卫回归失败（见 $OUT/preview_guard.log）"; tail -8 "$OUT/preview_guard.log"
fi

# ── L2：构建 + 栈深 ────────────────────────────────────────────────
step "L2 固件构建 + 栈深"
if ./idf.sh build >"$OUT/idf.log" 2>&1; then
  grep -E "binary size" "$OUT/idf.log" | tail -1 | sed 's/^/  /'
  pass "固件构建"
else
  bad "固件构建失败（见 $OUT/idf.log）"; tail -20 "$OUT/idf.log"; exit 1
fi
if python3 tools/stack_check.py >"$OUT/stack.log" 2>&1; then
  if grep -q "全部入口在预算内" "$OUT/stack.log"; then
    grep -E "合计|预算" "$OUT/stack.log" | sed 's/^/  /'
    pass "栈深在预算内"
  else
    bad "栈深超预算："; grep -E "合计" "$OUT/stack.log"; fi
else
  # 先看日志内容再归因（审计 M30）：stack_check.py 超预算时同样 return 1，以前一律报
  # "环境没激活"，把维护者引去折腾 IDF，而主任务栈 16384 B 才是硬约束。
  if grep -q "超出预算" "$OUT/stack.log"; then
    bad "栈深超预算："; grep -E "超出预算" "$OUT/stack.log" | head -10
  elif grep -q "栈检查未判定" "$OUT/stack.log"; then
    bad "栈深未判定：含无法静态解析的调整"; tail -5 "$OUT/stack.log"
  else
    bad "栈深检查跑不起来（先激活 IDF 环境或设置 FNOS_IDF_ENV）"; tail -5 "$OUT/stack.log"
  fi
fi

# ── L2.5：kk_ui 是否已出局（目标硬要求）────────────────────────────
# 判据分两层：① 目录必须消失；② **代码/构建**里不许再有 kk_* 的引用。
# 只查代码与构建文件（*.c/*.h/*.cpp/CMakeLists/Kconfig/*.sh/*.py），不查文档——
# "为什么换掉 kk_ui"这类说明性文字是有价值的，把散文一起当违规会逼人删注释，
# 检查也会因为自己提到 kk_ui 而永远不过（第一版就是这么写错的）。
step "kk_ui 出局检查"
if [ -d components/fnos_monitor/kk_ui ]; then
  bad "components/fnos_monitor/kk_ui 目录仍在（要求：全局只用 ui_kit/ LVGL 实现）"
else
  # 真正致命的两种引用：kk_ui/ 路径（include、CMake 源文件表）与 kk_xxx( 函数调用。
  # 本脚本自己要写这两个模式，必须排除自己——本机 BSD grep 的 --exclude 和 --include
  # 一起用时不生效（实测 2.6.0-FreeBSD），所以改用输出侧过滤。
  hits=$(grep -rInE --exclude-dir=build --exclude-dir=build-release --exclude-dir=.git \
              --exclude-dir=__pycache__ --exclude-dir=managed_components --exclude-dir=archive \
              --include=*.c --include=*.h --include=*.cpp --include=*.txt \
              --include=CMakeLists.txt --include=Kconfig --include=*.sh --include=*.py \
              -e 'kk_ui/|kk_[a-z_]+[[:space:]]*\(' components main tools 2>/dev/null \
         | grep -v '^tools/verify_all.sh:')
  if [ -z "$hits" ]; then pass "kk_ui 已删、代码与构建里无 kk_ 引用（ui_kit 为唯一实现）"; else
    bad "仍有 kk_ 代码引用："; printf '%s\n' "$hits" | sed 's/^/    /'
  fi
fi

# ── L3：实机 ──────────────────────────────────────────────────────
if [ "$FLASH" = 1 ]; then
  step "L3 烧录 + 串口 30s"
  if ./idf.sh -p "$PORT" flash >"$OUT/flash.log" 2>&1; then pass "烧录"; else bad "烧录失败"; tail -10 "$OUT/flash.log"; exit 1; fi
  if python3 tools/serial_capture.py --port "$PORT" --seconds 30 --out "$OUT/serial.log" >"$OUT/serial_capture.log" 2>&1; then
    if python3 tools/serial_check.py "$OUT/serial.log" >"$OUT/serial_check.log" 2>&1; then
      pass "当前启动建树/刷新可用、无关键故障标记（尚非实机屏幕验收）"
    else
      bad "串口启动证据不合格（见 $OUT/serial_check.log）"
    fi
  else
    bad "串口采集失败（见 $OUT/serial_capture.log）；部分日志不能放行"
  fi
fi

if [ "$PHOTO" = 1 ]; then
  step "L3 拍屏（手机取景）"
  if PATH="/opt/homebrew/bin:$PATH" capture_board_photo "$OUT" >"$OUT/photo.log" 2>&1; then
    pass "拍屏文件已解码：$OUT/board-photo.png（待人工看图，未判实机合格）"
  else
    bad "拍屏或PNG解码失败（见 $OUT/photo.log）"
  fi
fi

printf '\n================ 汇总：%d PASS / %d FAIL ================\n' "$ok" "$fail"
[ "$fail" -eq 0 ]
