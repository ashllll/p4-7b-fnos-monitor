#!/bin/bash
# 生成测试报告 docs/fnos-companion-test-report.md。
#
# 为什么是"生成"而不是手写：手写的报告一定会过期。这里把当次真实跑出来的结果、
# 版本号、文件大小和哈希直接写进文档，报告只描述本次执行证据，未运行与未判定项必须保留。
#
# 用法：bash tools/test_report.sh          （会跑 NAS 侧的全部检查，约 1 分钟）
#       bash tools/test_report.sh --quick  （跳过需要重新跑一遍测试套件的部分）
#
# 退出码：0=本次检查完成且通过；1=检查失败；2=未完成或参数/环境问题。
# 注意：它只做只读的检查与统计，不改任何源码；会写报告和临时测试产物；不操作真实NAS或开发板。
set -euo pipefail
cd "$(dirname "$0")/.." || exit 2
export PATH="$HOME/.local/bin:$PATH"
QUICK=0
FULL=0
HARD_FAIL=0
INCOMPLETE=0
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;;
    --full) FULL=1 ;;
    *) echo "Unknown argument: $a" >&2; exit 2 ;;
  esac
done

OUT=docs/fnos-companion-test-report.md
PKG=nas/fpk/nasscreencompanion
FPK=nas/fpk/nasscreencompanion.fpk
TMP="$(mktemp -d "${TMPDIR:-/tmp}/fnos-report.XXXXXX")" || { echo "Cannot create report workspace" >&2; exit 2; }
cleanup_report() {
  local rc=$?
  if [ "$rc" = 0 ]; then
    rm -rf "$TMP"
  else
    echo "本轮日志保留于 $TMP" >&2
  fi
}
trap cleanup_report EXIT

hdr() { printf '\n=== %s ===\n' "$*"; }

hdr "跑 NAS 侧端到端套件"
if [ "$QUICK" = "1" ]; then
  INCOMPLETE=1
  SUITE_LINE="（--quick：本次没有重跑，见上一次的报告）"
  SUITE_COUNT="?"
else
  SUITE_RC=0
  nas/fpk/test_lifecycle.sh >"$TMP/suite.log" 2>&1 || SUITE_RC=$?
  SUITE_COUNT=$(grep -oE '结果：[0-9]+ 项通过，[0-9]+ 项失败' "$TMP/suite.log" | tail -1 || true)
  # 套件异常时保留本轮私有日志目录，不覆盖其它轮次的证据。
  # （真发生过一次 130/1，紧接着连跑两次都是 131/0，日志已经没了）
  if [ "$SUITE_RC" = 2 ]; then
    INCOMPLETE=1
    echo "  ! 套件未完成，日志位于 $TMP/suite.log"
  elif [ "$SUITE_RC" != 0 ] || ! grep -qE '^结果：[0-9]+ 项通过，0 项失败$' <<<"${SUITE_COUNT:-}"; then
    HARD_FAIL=1
    echo "  ! 套件有失败项，日志位于 $TMP/suite.log"
    grep -E '✗' "$TMP/suite.log" | head -5 | sed 's/^/      /' || true
  fi
  # 去掉"结果："前缀，免得写进句子里变成"…验证：结果：93 项…"
  # **汇总数必须等于真的打出来的 ✓ 个数。**
  # 第 42 轮把套件里的一个阶段整段挪到了文件第一行（`#!/bin/bash` 之前），
  # 脚本照样能跑（bash 不看 shebang），那一段就静默地没执行 —— 而汇总数跟着少，
  # 131 个 ✓ 配 131 的汇总，**看上去完全正常**。是人工数 ✓ 才发现的。
  # 另外 PASS/FAIL 是在子 shell 里加的话也会丢，症状一样：绿得看不出来。
  # **别用 `grep -P`**：macOS 自带的 BSD grep 没有这个选项，它会直接报错退出，
  # 于是计数变成 0、与汇总数一比就误报（或者更糟：被 `|| echo 0` 吞掉变成永远相等）。
  # 用 -F 配一个字面量（前导两个空格 + 绿 ✓ 的转义序列）最稳。
  SUITE_TICKS=$(grep -cF "$(printf '  \033[32m✓')" "$TMP/suite.log" 2>/dev/null || true)
  SUITE_PASS=$(sed -nE 's/.*结果：([0-9]+) 项通过.*/\1/p' "$TMP/suite.log" | tail -1 || true)
  SUITE_LINE="${SUITE_COUNT#结果：}"
  SUITE_LINE="${SUITE_LINE:-没有取到结果}"
  if [ "$SUITE_RC" != 0 ]; then
    SUITE_LINE="$SUITE_LINE —— **进程异常退出（${SUITE_RC}）**"
  fi
  if [ -z "${SUITE_PASS:-}" ] || [ "$SUITE_PASS" -eq 0 ] || \
     [ "$SUITE_PASS" != "$SUITE_TICKS" ]; then
    echo "  ✗ 套件汇总说 ${SUITE_PASS} 项通过，但日志里只有 ${SUITE_TICKS} 个 ✓"
    echo "      说明有用例根本没执行（比如整段被挪到了 shebang 之前），或者计数丢了"
    # **不只打印，还要落到报告里、并且影响退出码**——否则又是一条"喊了没人听"的检查
    SUITE_LINE="${SUITE_LINE} —— **异常：日志里只有 ${SUITE_TICKS} 个 ✓，有用例没执行**"
    HARD_FAIL=1
  fi
fi
echo "$SUITE_LINE"

hdr "跑各个单项检查"
CHECK_INDEX=0
run_check() {  # run_check <名字> <命令...>
  local name="$1"; shift
  local how
  printf -v how '%q ' "$@"
  CHECK_INDEX=$((CHECK_INDEX+1))
  local log="$TMP/check-$CHECK_INDEX.log"
  if "$@" >"$log" 2>&1; then
    echo "  ✓ $name"
    echo "$name|通过|$how" >>"$TMP/checks"
  else
    echo "  ✗ $name"
    # 只报"失败"没用——把那一项的**最后几行**贴出来，读报告的人才知道去查什么。
    # （有一次内存判定短暂报红，就是靠这一步才看出它其实是通过的、只是当时机器忙。）
    tail -3 "$log" 2>/dev/null | sed 's/^/      /'
    echo "$name|失败|$how" >>"$TMP/checks"
    # 有检查失败时退出码不能是 0：**报告是给人和 CI 看的，退出码是给脚本看的**，
    # 两者说法不一致时，被信的一定是退出码。
    HARD_FAIL=1
  fi
}
: >"$TMP/checks"
# 放在最前面：产物过期时的"全绿"是对旧代码说的，先确定验的是当前代码
run_check "产物不比源码旧（三套产物）" python3 tools/freshness_check.py
run_check "字段契约（板子读的字段 NAS 是否都在）" python3 nas/fpk/contract_check.py
run_check "向导表单 schema" python3 nas/fpk/wizard_check.py "$PKG"
run_check "manifest 取值与包内结构" python3 nas/fpk/manifest_check.py "$PKG"
run_check "每请求内存判定" python3 nas/fpk/mem_check.py 3000
run_check "匿名大清单持续发布与 HTTP 内存判定" python3 nas/fpk/mem_check.py 3000 --entities 1024 --publish-every 10
# plan_check 读的正是**这份报告**里的「本轮断言条数 / 失败数」，所以它只能在报告写完之后跑。
# 以前它排在报告重写之前（审计 M51）：跑的是上一轮的结论 —— 上一轮要是红的，这一轮即使
# 已经修好了也会被记一行"失败"并置 HARD_FAIL，而写完之后那次复跑只能追一条 ⚠ 注脚，
# **改不了那一行，也改不了退出码**。现在这里只占位，结论在报告写完后回填（见文件末尾）。
echo "计划文档与现状一致|__PLAN_PENDING__|python3 tools/plan_check.py" >>"$TMP/checks"
run_check "证书指纹三方一致（固件 / 服务端 / openssl）" bash tools/certcheck/run.sh
run_check "连接失败原因标签跨文件契约" python3 tools/reason_check.py
run_check "管理页 ↔ 服务端接口字段名" python3 nas/fpk/api_check.py
run_check "权限分类器（矩阵的每一格都来自它）" python3 nas/fpk/perm_check.py
# 主机预览：方案文档的状态行里写着"主机预览样张与断言全通过"，但报告此前**根本没跑它**
# ——又是"报告替没跑过的检查背书"。它同时是四条版面审计（字形/溢出/越界/压盖）的入口。
run_check "主机预览（四条版面审计 + 配对流程断言）" bash tools/preview/run.sh
# 长测不是交付物，于是它有**三种**状态，不能只塞进 ✓/✗ 两格：
#   rc 0 = 日志新鲜、服务端Python副本一致、样本健康 → 通过此局部门
#   rc 2 = 日志缺失/过期/未来时间 → **未判定**：不是本轮缺陷，
#          但绝不能记成 ✓——那等于报告替一条没有证据的结论背书。
#   rc 1 = 源码身份缺失/不一致、样本不足/异常等 → 失败。
#   此门不证明进程仍在运行，也不替代72小时或设备验收。
SOAK_NAME="本地长测日志（源码一致、样本健康）"
SOAK_LOG="$TMP/soak.log"
SOAK_RC=0
python3 tools/soak_check.py >"$SOAK_LOG" 2>&1 || SOAK_RC=$?
if [ "$SOAK_RC" = "0" ]; then
  echo "  ✓ $SOAK_NAME"
  echo "$SOAK_NAME|通过|python3 tools/soak_check.py" >>"$TMP/checks"
elif [ "$SOAK_RC" = "2" ]; then
  INCOMPLETE=1
  echo "  ! ${SOAK_NAME}：未判定（长测日志证据不足）"
  head -4 "$SOAK_LOG" | sed 's/^/      /'
  echo "$SOAK_NAME|**未判定**（长测日志证据不足，不算失败也不算通过）|python3 tools/soak_check.py" >>"$TMP/checks"
else
  echo "  ✗ $SOAK_NAME"
  tail -3 "$SOAK_LOG" | sed 's/^/      /'
  echo "$SOAK_NAME|失败|python3 tools/soak_check.py" >>"$TMP/checks"
  HARD_FAIL=1
fi
run_check "Python 开发模式零警告（未关闭的 socket/文件）" python3 nas/fpk/devmode_check.py 30
run_check "截断真的发生了（六段各喂 50 条）" python3 nas/fpk/collector_check.py
run_check "满载状态帧能被固件的 parse_status() 吃下" bash tools/parsecheck/run.sh
# 本轮修复的回归（Codex 复核打回的三条 + M31/M36/M57）。全部在临时目录里跑，用桩 kill /
# 桩 systemctl / 派生的预览副本，不碰真 NAS、不碰真服务、不删仓库外的数据。
# 运行时间：生命周期约 2 分钟、令牌路径约 10 秒、预览守卫约 2 分钟（它整跑一次预览）。
run_check "停止失败时不删记录、不写配置（M21/M25/M26）" bash nas/fpk/test_process_lifecycle.sh
run_check "配网提交代次、迟到事件与 RPC 失败" python3 tools/test_wifi_submission.py
run_check "报告入口错误传播与未完成状态（纯桩）" python3 -W error::ResourceWarning tools/test_report_entry.py
run_check "空原因集合与缺预览配置负控" python3 tools/test_host_gate_failures.py
run_check "发布配置恢复与工具失败边界（无设备）" python3 -W error::ResourceWarning tools/test_gate_failures.py
run_check "Manifest 包身份与字段形状负控" python3 -W error::ResourceWarning tools/test_manifest_failures.py
run_check "PID 写失败与卸载收尾（无 NAS）" python3 nas/fpk/test_lifecycle_failures.py
run_check "API 与向导规则负控（无 NAS）" python3 nas/fpk/test_validation_gates.py
run_check "检查器故障收尾（无 NAS）" python3 -W error::ResourceWarning nas/fpk/test_tool_cleanup.py
run_check "配对状态边界（无网络）" python3 tools/test_pair_state.py
run_check "配对存储读取故障（无设备）" python3 tools/test_pair_nvs.py
run_check "配对存储屏障与模拟重启（无设备）" python3 tools/test_pair_nvs_transaction.py
run_check "串口采集复位与收尾（模拟串口）" python3 tools/test_serial_capture.py
run_check "SSH 与远程安装脚本（纯桩）" python3 -W error::ResourceWarning nas/fpk/test_ssh_scripts.py
run_check "配对持久化与锁失败（无设备）" python3 tools/test_pair_commit.py
run_check "TLS 探针重入与诊断缓冲（无网络）" python3 tools/test_tls_probe_guard.py
run_check "UI 原子请求与刷新定时器重试（无设备）" python3 tools/test_ui_requests.py
run_check "启动定时器失败与资源回收（无设备）" python3 tools/test_main_timers.py
run_check "照片解码预算与坏图拒绝（匿名PNG）" python3 -W error::ResourceWarning tools/test_photo_check.py
run_check "新鲜度证据缺失拒绝（匿名临时文件）" python3 tools/test_evidence_gates.py
run_check "证书辅助信息真实 mbedTLS 主机解析（无 NAS）" python3 tools/test_cert_info.py
run_check "Wi-Fi 存储屏障、读取拒绝与模拟重启（无设备）" python3 tools/test_wifi_store.py
run_check "串口命令边界、数字溢出与口令行清理（无设备）" python3 tools/test_wifi_cli.py
run_check "配对诊断缓冲任务边界（无设备）" python3 tools/test_pair_diag.py
run_check "配对配置快照与锁失败（无设备）" python3 tools/test_pair_snapshot.py
run_check "升级回退点与恢复标志（无 NAS）" python3 nas/fpk/test_upgrade_safety.py
run_check "全地址监听预检与未知拒绝（无 NAS）" python3 nas/fpk/test_port_preflight.py
run_check "历史与实时合并、游标失效及端点重置（无设备）" python3 tools/test_history_merge.py
run_check "历史回填时间边界（无设备）" python3 tools/test_history.py
run_check "性能样本完整性（无设备）" python3 tools/test_perf_samples.py
run_check "大小栈帧与未知调整负控" python3 tools/test_stack_frames.py
run_check "拍屏错误处理（模拟，无设备）" python3 tools/test_page_shot.py
run_check "TLS 故障拒绝降级、令牌并发与安全日志" python3 nas/fpk/test_server_safety.py
run_check "遥测发布所有权、编码复用与停止屏障（无 NAS）" python3 nas/fpk/test_telemetry_publication.py
run_check "令牌 fail-closed 安装/升级/故障路径（M31）" python3 nas/fpk/test_install_token.py
run_check "Release 断言与坏夹具拒绝（无设备）" bash tools/preview/test_release_asserts.sh
run_check "预览输出目录守卫 + 分支变量按阶段隔离（M36/M57）" bash tools/preview/test_run_guard.sh
# 全量发布构建要两三分钟，默认不跑——但**报告里不能因此写"通过"**。
# 之前那一行是写死的"通过"，等于报告替一个没跑过的检查背书。
RELEASE_ROW=""
if [ "$FULL" = "1" ]; then
  # 直接跑、只要结论：不要再往"检查项"表里插一行——那会让报告里出现两条同名的行。
  if bash tools/release_check.sh >"$TMP/release.log" 2>&1; then
    echo "  ✓ 通用发布构建 + 秘密扫描"
    RELEASE_ROW="通过（本次实跑）"
  else
    echo "  ✗ 通用发布构建 + 秘密扫描"
    tail -3 "$TMP/release.log" | sed 's/^/      /'
    RELEASE_ROW="**失败**（本次实跑）"
    HARD_FAIL=1
  fi
else
  INCOMPLETE=1
  RELEASE_ROW="**本次未运行**（要两三分钟）——跑 \`bash tools/release_check.sh\` 或 \`bash tools/test_report.sh --full\`"
fi
if command -v riscv32-esp-elf-objdump >/dev/null 2>&1; then
  run_check "静态栈预算" python3 tools/stack_check.py
else
  echo "  ! 没有 RISC-V 工具链，跳过栈预算检查"
  echo "静态栈预算|**未运行**（缺少工具链）|python3 tools/stack_check.py" >>"$TMP/checks"
  HARD_FAIL=1
fi

hdr "统计产物"
FPK_SHA=$(shasum -a 256 "$FPK" 2>/dev/null | awk '{print $1}' || true)
FPK_SIZE=$(stat -f%z "$FPK" 2>/dev/null || echo '?')
FW_SIZE=$(stat -f%z build/fnos_monitor.bin 2>/dev/null || echo '?')
FW_SHA=$(shasum -a 256 build/fnos_monitor.bin 2>/dev/null | awk '{print $1}' || true)
FW_DATE=$(stat -f '%Sm' -t '%Y-%m-%d %H:%M' build/fnos_monitor.bin 2>/dev/null || echo '?')
FNPACK_VER=$(fnpack 2>&1 | grep -oE 'Version [0-9.]+' | head -1 || true)
PY_VER=$(python3 -V 2>&1)
NODE_VER=$(node --version 2>/dev/null || echo '（没有 node）')
MACOS=$(sw_vers -productVersion 2>/dev/null || echo '?')
FILES=$(ls -1 "$PKG/cmd" | wc -l | tr -d ' ')

hdr "写报告"
{
  echo "# NAS 屏幕伴侣：本地测试报告"
  echo
  echo "本文件由 \`bash tools/test_report.sh\` 生成，**不要手改**——改的是检查本身，报告跟着变。"
  echo "生成时间：$(date '+%Y-%m-%d %H:%M')（本机时区）"
  echo
  echo "## 结论一句话"
  echo
  echo "NAS 侧应用包本次端到端验证结果：${SUITE_LINE}。"
  echo "安装到真 NAS、以及开发板与 NAS 的真实配对，**尚未进行**（见最后一节）。"
  echo
  echo "## 环境"
  echo
  echo "| 项 | 值 |"
  echo "| --- | --- |"
  echo "| 开发机 | macOS $MACOS |"
  echo "| Python | $PY_VER |"
  echo "| Node | $NODE_VER |"
  echo "| fnpack | ${FNPACK_VER:-未取到} |"
  echo "| 目标运行时（NAS） | \`install_dep_apps = python312\` |"
  echo "| 目标系统（NAS） | 飞牛 fnOS / Debian 12 |"
  echo
  echo "## 产物"
  echo
  echo "| 产物 | 大小 | SHA-256 |"
  echo "| --- | --- | --- |"
  echo "| \`$FPK\` | $FPK_SIZE 字节 | \`$FPK_SHA\` |"
  echo "| \`build/fnos_monitor.bin\`（开发板固件，${FW_DATE}） | $FW_SIZE 字节 | \`$FW_SHA\` |"
  echo
  echo "包内共 \`cmd/\` $FILES 个文件（含共用的 \`_common.sh\`）。"
  echo
  echo "## 检查项与结果"
  echo
  echo "| 检查 | 覆盖什么 | 结果 | 怎么重跑 |"
  echo "| --- | --- | --- | --- |"
  echo "| 端到端生命周期套件 | 打包→按真机布局解开→安装/启动/改设置/升级/卸载；权限矩阵；TLS 与指纹；协议字段契约；明文引导通道；端口冲突不越界；向导表单；管理页 JS；平台兼容 | $SUITE_LINE | \`nas/fpk/test_lifecycle.sh\` |"
  while IFS='|' read -r name res how; do
    if [ -z "$how" ]; then
      how='—'; NOHOW="${NOHOW:-}${name}、"
    else
      how="\`$how\`"
    fi
    echo "| $name | 见「每一项都在验什么」 | $res | $how |"
  done <"$TMP/checks"
  # 每个跑过的检查都得有"怎么重跑"。漏了会渲染成一个「—」的单元格——不难看，
  # 但那一项就**没法照着复现**了，而这张表的意义正是"每一条都能自己再跑一遍"。
  if [ -n "${NOHOW:-}" ]; then
    echo "  ✗ 这些检查没有写「怎么重跑」：${NOHOW%、}"
    echo "      检查 run_check 命令记录，否则报告里那一格是「—」"
    HARD_FAIL=1
  fi
  echo "| 板子动作序列（配对 + 证书固定） | 明文取证书→自算指纹→固定建 TLS→换令牌→带令牌取数；含「固定别的证书必须失败」及其对照 | 随生命周期套件验证：$SUITE_LINE | \`python3 nas/fpk/board_sim.py --host <NAS> --port 8798 --pair-code <码>\` |"
  echo "| 通用发布构建 + 秘密扫描 | 不含本地配置也能编出来；产物里搜不到 SSID/口令/令牌/NAS 地址 | ${RELEASE_ROW} | \`bash tools/release_check.sh\` |"
  echo
  echo "## 每一项都在验什么"
  echo
  echo "- **端到端生命周期套件**（\`nas/fpk/test_lifecycle.sh\`）：先 \`fnpack build\`，把产出的 \`.fpk\` 解到临时目录、**复刻 \`/var/apps/{appname}/\` 的真实布局**（\`cmd/\` 一套、\`target/\` 一套、\`var/\`、\`etc/\`），再依次跑安装、启动、幂等启动、管理面、配对与令牌、改设置换端口、升级（配置备份 + 令牌保留）、卸载、端口冲突、Socket 建不起来时的降级。里面钉着几条容易漏的：管理页显示的证书指纹必须等于 \`openssl x509 -noout -fingerprint -sha256\`（板子固定的就是这个值，算错等于信错证书）；关掉 TLS 再打开后指纹必须**完全不变**（否则已配对设备全部失效）；旧固件依赖的 \`/api/v1/status\`、\`history\`、\`health\` 字段名与类型不许变（只能加字段）。"
  echo "- **字段契约**（\`contract_check.py\`）：固件用 cJSON 按字段名取值，取不到就是 0——不报错、不打日志。这个脚本从固件源码**现抓**它读了哪些字段（不手抄清单），逐项到 NAS 的产出里核对。"
  echo "- **manifest 校验**（\`manifest_check.py\`）：\`fnpack build\` 只查"必要字段在、格式对"，不查取值。这里按官方文档核 \`platform\`/\`ctl_stop\`/\`service_port\`/\`install_dep_apps\` 等，并交叉核对 \`desktop_applaunchname\` 确实存在于 \`ui/config\` 的入口 ID 里（对不上＝桌面卡片打不开）。"
  echo "- **向导 schema**（\`wizard_check.py\`）：安装向导是用户看到的第一屏。核字段类型、选项、\`wizard_\` 前缀、校验规则，以及\`initValue\` 能不能通过自己的 \`rules\`（不能＝用户什么都不改就被拦下）。"
  echo "- **管理页 JS**（\`page_check.js\`）：在 Node + 最小 DOM 垫片里跑真实脚本、喂真实响应、把每个按钮点一遍，并对渲染结果做断言——JS 读不存在的属性只得到 \`undefined\`，不会报错。"
  echo "- **每请求内存判定**（\`mem_check.py\`）：分别用空快照与每类 1024 项匿名清单打 3000 个 HTTP 请求；后者同时持续生产发布，检查完整清单与代次，RSS 判据取后 1/3 比中间 1/3。不代替真实 NAS 长测。"
  echo "- **静态栈预算**（\`stack_check.py\`）：从 ELF 算各任务调用链的栈深下界。本项目被栈溢出烧过两次，而主机预览栈是 MB 级、永远测不出来。"
  echo "- **产物不比源码旧**（\`tools/freshness_check.py\`）：这个项目有三套产物、三条编译路径，验证成本差得很远（改文案跑预览几秒，\`./idf.sh build\` 两分钟起）。便宜的那条会被反复跑、贵的那条容易被跳过，于是出现最坏的情况：**源码里有编不过的东西，而所有检查都是绿的**——因为它们检查的是上一次编出来的二进制。这个脚本只比 mtime，一秒出结果。它第一次跑就抓到三套产物全部过期（其中固件比源码旧 2 分钟、应用包比 \`cmd/_common.sh\` 旧 3 分钟），也就是说那一轮的"全绿"里有一部分是对旧代码说的。"
  echo "- **证书指纹三方一致**（\`tools/certcheck/run.sh\`）：指纹是整套信任模型里唯一的人工锚点——用户在 NAS 管理页看一串、在板子屏幕看一串，一样才放行，而这两个值由**两段完全独立的代码**算出（服务端纯 Python、板端 C + mbedtls）。这个脚本把固件里那个 \`pem_fingerprint()\` **原样抠出来**编到主机上跑（不抄一份，抄了就等于测手抄的那份），和 Python 版、和 \`openssl x509 -noout -fingerprint -sha256\` 三方对照；另外测 CRLF 换行与"PEM 里带证书链"两个边界。它第一次跑就抓到固件把整条链的 base64 拼起来、链一长就返回 false，而服务端是取第一段——两端会对不上，用户只会看到"证书解不开"。"
  echo "- **满载状态帧能被固件的 parse_status() 吃下**（\`tools/parsecheck/\`）：\`contract_check.py\` 只核对"字段名在不在"，**没有任何检查验证板子能不能解析满载的那一帧**——而这是固件里跑得最勤的一段代码（每秒一次），却从来没在设备之外执行过（主机预览把 \`fnos_data\` 整个换成了替身）。做法与 certcheck 一致：从 \`fnos_data.c\` 与 \`fnos_snapshot.c\` 里**原样抠出**解析那一串函数（解析层已搬进快照模块，两个文件都要看）编译到主机（cJSON 用工程里那一份）。满载口径不写死：每项字节数取自 \`contract_check.py\` 的 \`PER_ENTRY\`，预算取自 \`sdkconfig\` 的 arena 上限，段内条数按两者反算（取不到就报错退出——旧版会退化成 \`10**9\`，把检查跑成永久挂起）。再跑一次**容量探针**：从高到低试几档填充度，量出「板子能解析的最大帧」并打印（1 MiB 的预算装不下 1 MiB 的 payload——arena 还要装结构体向量和每串账头）。断言解析成功、各段计数与上限一致、历史回填的行数/时间跨度/环形缓冲回读顺序与窗口夹取、**且截断过的每个字符串都是合法 UTF-8**。"
  echo "- **截断真的发生了**（\`nas/fpk/collector_check.py\`）：\`contract_check.py\` 比的是 \`DEFAULT_LIMITS\` 这个**常量**和板子的数组容量——它保证"两边数字一样"，但**保证不了代码真的照着这个数字裁**。证伪时把 \`vols\` 的截断去掉，两个检查**都是绿的**。而这类事在本机测不出来：macOS 上只有一两个卷、零个阵列、零个温度传感器，**数量本来就低于上限**，裁不裁看起来一模一样。所以这里绕开真实数据，直接给 \`_trim()\` 喂 50 条合成数据（快、确定、不可能是空断言），再**静态核一遍六个调用点都真的走了 \`_trim()\`**、且没有漏网的硬编码截断。"
  echo "- **Python 开发模式零警告**（\`nas/fpk/devmode_check.py\`）：**静态检查和断言都看不见"没说出口的问题"**。开发模式（\`-X dev\`）会把 \`ResourceWarning\`（未关闭的 socket/文件）、弃用调用这些默认被静默吞掉的信号打出来——而这类东西正好是长跑才会发作：单次请求看不出异常，跑一天就是句柄耗尽。这个脚本按真机布局摆好包、用 \`python3 -X dev\` 起服务、对管理面 + TLS 面 + 明文引导面各打若干次、断言输出里零 \`Warning\`。证伪：在 \`do_GET\` 里故意漏一个未关闭的 socket → 报 \`ResourceWarning: unclosed <socket.socket fd…>\`。"
  echo "- **本地长测**（\`tools/soak_check.py\`）：方案文档里写着「服务侧长跑已完成」，而在此之前这件事一直靠**肉眼看日志尾部**。第 18 轮吃过一次亏——那个跑了十几个小时的实例，跑的其实是一份被破坏过的副本，日志每一行都漂漂亮亮地写着「管理面=200 TLS=200 明文引导=200」，**它只是一直在测旧的、坏的那份代码**。所以这个脚本核对日志新鲜度、全部服务端 Python 模块副本、递增采样及最近端点/资源趋势。日志mtime不证明进程仍在运行，也不证明72小时或设备验收。"
  echo "- **主机预览**（\`tools/preview/run.sh\`）：把真实的 \`fnos_ui.c\` + \`ui_kit/\` + 字库编到 macOS 上渲染当前样张矩阵，四条审计全程盯着——**字形覆盖**（缺字直接 abort）、**标签溢出**（\`lv_text_get_size\` 比控件高）、**子对象越界**、**可点击控件互相压盖**，另有 11 条连接失败原因的渲染断言与整套配对流程断言。它是"用户根本看不见"这类问题的唯一拦截点。"
  echo "- **权限分类器**（\`nas/fpk/perm_check.py\`）：P0 要交的那张「来源 / 权限矩阵」，每一格都来自 \`classify_read()\` / \`classify_statvfs()\`。**判错的代价是方向性的**——把"权限不足"报成"不存在"，用户会去查驱动和硬件；反过来用户会去翻权限配置，两种都白折腾。开发机上测不了 NAS 的结论（macOS 没有 /proc、/sys），但分类器是纯逻辑，用临时文件树 + \`chmod 000\` 造出真 EACCES 就能验。"
  echo "- **管理页 ↔ 服务端的接口字段名**（\`nas/fpk/api_check.py\`）：页面发出去的字段名服务端不认识时**不会报错**——\`_apply_config()\` 是逐键判断的，多一个没人认的键它一声不吭。用户看到的是"点了保存、提示已保存、而设置没变"。\`page_check.js\` 抓不到这个（它喂的是预制响应，不看请求体），所以从两边现抓：页面里 \`get/post\` 的路径与请求体键名，服务端的路由表与各端点认的键，要求页面 ⊆ 服务端。"
  echo "- **连接失败原因标签的跨文件契约**（\`tools/reason_check.py\`）：板子连不上的原因差别很大——网线没插和"NAS 换过证书"要做的处理完全不同。所以 \`fnos_data.c\` 失败时写一个短标签（\`tls handshake\`、\`no cert\`…），\`fnos_ui.c\` 的 \`link_reason()\` 把它翻成人话显示在总览页。**两个列表分居两个文件，谁改了另一边不知道**；漂移的后果很隐蔽——不认识的标签让 \`link_reason()\` 返回 NULL，那行字**整条消失**，界面看上去只是"没写原因"，用户不知道该干什么。这个脚本从两边现抓集合（不手抄清单）核对"产出 ⊆ 已翻译"与"已翻译 ⊆ 产出"。主机预览另有运行时那一半：逐个标签断言翻得出话、每句都不一样、且真的画到了屏幕上。"
  echo "- **计划文档与现状一致**（\`plan_check.py\`）：核 \`docs/fnos-companion-app-plan.md\` 里的事实性声明——引到的路径真的存在、字段数等于 \`board_fields.json\` 的实际条目数、包大小等于 \`.fpk\` 的实际字节数、断言条数与本报告一致。过期的数字比没有数字更糟：读者不会去核对它。"
  echo "- **停止失败时不删记录、不写配置**（\`nas/fpk/test_process_lifecycle.sh\`）：复核打回过一条真缺陷——\`stop_process\` 在 SIGKILL 之后仍然删掉 PID 记录与 socket，\`uninstall_callback\` 随后就\"看不到进程\"、报\"已释放端口\"、退出码 0；而进程其实还活着，端口也被占着。这条回归在临时目录里复刻真机包布局，用 \`BASH_ENV\` + \`enable -n kill\` + PATH 上的 \`kill\` 桩\"只对那一个 pid 撒谎\"来造\"杀不掉\"的场景（不碰任何真实进程或服务），断言：停止失败 → 非零退出、PID 记录与 socket 保留、\`status\` 仍报进程存在、卸载不报\"已释放\"；改配置时旧实例停不掉 → 配置字节级不变、不报\"设置已生效\"、不起第二个实例；正常停止与启动失败收尾照旧。"
  echo "- **令牌 fail-closed 安装/升级/故障路径**（\`nas/fpk/test_install_token.py\`）：默认安装此前是\"文档说会生成令牌、实际上 \`nas/fnos-agent.service\` 连 \`EnvironmentFile\` 都没有\"，于是拒启动或者更糟——为了跑起来加 \`--insecure-no-auth\`。这条回归用临时根 + \`systemctl\` 桩 + 心跳健康桩跑首次安装/升级/重签/卸载：首装生成 48 位十六进制令牌文件（0600）、升级时令牌逐字节不变且权限被修回、输出里只有指纹 sha256[:8] 而**从不打印令牌本身**、单元文件缺 \`EnvironmentFile\` 或带 \`--insecure-no-auth\` 时非零退出且一个文件都不落盘、健康检查返回 200（说明真的没在鉴权）也算失败。"
  echo "- **预览输出目录守卫 + 分支变量按阶段隔离**（\`tools/preview/test_run_guard.sh\`）：两处\"脚本自己以为自己是对的\"的地方。一是 \`run.sh\` 只挡了 \`..\` 与绝对路径，输出目录的**父目录是软链**时仍能 \`rm -rf\` 到仓库外——回归用临时哨兵目录证明越界路径被拒、哨兵文件活着。二是 全部 \`PREVIEW_*\` 分支变量原先只清 5 个，调用方的脏环境会让夹具/满载阶段跑错分支——回归把 全部变量全设成恶意值，用按二进制路径改写的预览副本记录每个阶段真实收到的环境，断言每个阶段只拿到它自己设的那个（\`git show 53e1bf8\` 的旧脚本作为负控：一个 \`env -u\` 都没有，全部全泄漏）。"
  echo
  echo "## 本地**没有**覆盖到的（都需要真机）"
  echo
  echo "| 项 | 为什么本地测不了 | 怎么验 |"
  echo "| --- | --- | --- |"
  echo "| 在干净 NAS 上安装/启动/停止/升级/卸载 | 需要真的 fnOS | 应用中心「手动安装」\`$FPK\` |"
  echo "| Linux 包用户身份下的 \`/proc\`、\`/sys\`、卷容量可读性 | 本机是 macOS，采集段本来就缺 | 装机后看管理页「能力矩阵」标签页 |"
  echo "| 统一网关的实际转发与身份注入 | 需要 fnOS 的网关 | 从飞牛桌面打开应用 |"
  echo "| 开发板与真实证书的握手、配对、断线恢复 | 需要 ESP32-P4 硬件 | 板子上配对 + 拔网线 |"
  echo "| 24 小时稳定性（含「证书错误」「拔出网线」场景） | 需要真机长跑 | 装机后挂一天 |"
  echo "| ARM 架构支持 | 未在 ARM 上装过 | 当前 \`platform = x86\`，验过再放宽 |"
  echo "| 上架材料里的真实截图 | 需要真机画面 | P4 提交前 |"
  echo
  echo "## 已知的、刻意的未完成项"
  echo
  echo "- \`manifest\` 里 \`maintainer\`/\`maintainer_url\`/\`distributor\`/\`distributor_url\` 仍是「待填写」占位。"
  echo "  \`python3 nas/fpk/manifest_check.py --strict\` 现在会因此报错——**这是故意的**，提交上架前必须填掉。"
  echo "- 图标是自绘的占位设计（深色底板 + 蓝色显示器 + 三根负载条），正式提交前应换设计稿。"
  echo "- 容器（Docker）采集默认关闭，SMART/硬盘健康首版不承诺。"
} >"$OUT"
echo "写出 $OUT"
# plan_check 里有一条是"文档写的断言条数 == 测试报告里的条数"，而这份报告是在上一步
# 才重写的：第 97 行那次跑的是**上一轮**的报告，本轮条数变了也照样绿（审计 M51）。
# 写完报告再核一次，这次才对得上；不一致就当场改判，并说明以哪段为准。
# plan_check 现在只在这里跑一次：报告已经写完，它读到的"断言条数 / 失败数"就是本轮的。
# 结论**回填**进上面那张表（占位符 __PLAN_PENDING__），并直接决定退出码 —— 表格、文案、
# 退出码三者说的是同一件事（审计 M51：以前三者会互相矛盾，而表格那一行是错的）。
if python3 tools/plan_check.py >"$TMP/plan2.log" 2>&1; then
  PLAN_RES="通过（本报告写完后复跑）"
  echo "  ✓ 计划文档与现状一致（写完本轮报告后复跑）"
else
  PLAN_RES="**失败**（写完本轮报告后复跑，见下方注脚）"
  HARD_FAIL=1
  echo "  ✗ 计划文档与现状一致（写完本轮报告后复跑）"
  tail -5 "$TMP/plan2.log" | sed 's/^/      /'
fi
python3 - "$OUT" "$PLAN_RES" "$TMP/plan2.log" <<'PY'
import io, os, sys
path, res, log = sys.argv[1], sys.argv[2], sys.argv[3]
with io.open(path, encoding="utf-8") as src:
    text = src.read()
if "__PLAN_PENDING__" not in text:
    sys.exit("报告里没有 __PLAN_PENDING__ 占位符——plan_check 的结论没地方落")
text = text.replace("__PLAN_PENDING__", res)
if "失败" in res:
    with io.open(log, encoding="utf-8", errors="replace") as src:
        tail = "\n".join(src.read().splitlines()[-5:])
    text += ("\n> ⚠ 上面「计划文档与现状一致」这一行是**本轮报告写完、plan_check 复跑之后**回填的："
             "它没过，所以本次报告不能被当成全绿（退出码同样非零）。复跑输出：\n\n"
             "```\n" + tail + "\n```\n")
import tempfile
fd, tmp = tempfile.mkstemp(prefix=".report-", dir=os.path.dirname(path) or ".")
try:
    with io.open(fd, "w", encoding="utf-8") as dst:
        dst.write(text)
    os.replace(tmp, path)
finally:
    if os.path.exists(tmp):
        os.unlink(tmp)
PY
echo "回填 plan_check 结论：$PLAN_RES"
if [ "${HARD_FAIL:-0}" != "0" ]; then
  echo "！本次报告里有硬失败项（见上面 ✗），报告仍已写出——别把它当全绿看"
fi
if [ "$HARD_FAIL" != 0 ]; then exit 1; fi
if [ "$INCOMPLETE" != 0 ]; then
  echo "本轮证据未完成（跳过或未判定项），退出码 2；不能当作全量验收通过"
  exit 2
fi
exit 0
