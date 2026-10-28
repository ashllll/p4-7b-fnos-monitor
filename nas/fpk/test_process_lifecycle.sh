#!/bin/bash
# 审计 M21/M25/M26 回归：停止失败时不许删记录、不许写配置、不许报"已释放"。
#
# 复核意见（Codex 打回第 2/3 条）指出三处假绿：
#   · stop_process 在 SIGKILL 之后进程还在时照样删 PID/socket，于是 uninstall 看到
#     is_running=false，报"已释放端口与 Socket"并 exit 0；
#   · abort_started 同样在没确认死亡的情况下删记录，"启动失败"之后进程还在占端口；
#   · config_callback 在 service_stop 失败后继续 apply_config，旧实例内存里的旧值
#     随后把新配置盖回去，用户看到"设置已保存"而生效的是旧值。
#
# 这条回归在**临时包目录**里跑真实的生命周期脚本（不碰真 NAS、不碰 8798/8799、
# 不碰系统服务），用一个 PATH 上的 kill 桩模拟"没权限/杀不掉"，并且对每个修复都跑
# 一遍 53e1bf8 的**旧脚本**做负控：旧脚本必须表现出假绿，新脚本必须如实报错。
#
# 桩的使用方式：`enable -n kill` 关掉 bash 内建 kill（在 BASH_ENV 里做），这样脚本里
# 的 `kill -0/-TERM/-KILL` 会走到桩上；桩只对 PID 文件里记着的那一个 pid 说谎
# （-0 说活着，TERM/KILL 一律失败），其他 pid 也拒绝，不向外转发信号。
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PKG="$REPO/nas/fpk/nasscreencompanion"
BASE_REV=53e1bf868bee0e2b66b05a8df8df342ba6ee29a4
FAILED=0

say() { echo "[m21/m25/m26] $*"; }
check() {   # $1=名字 $2=0/1 $3=说明
  if [ "$2" = 0 ]; then
    echo "✓ $1${3:+　—— $3}"
  else
    echo "✗ $1${3:+　—— $3}"
    FAILED=$((FAILED + 1))
  fi
}

WORK="$(mktemp -d)"
case "$WORK" in
  /tmp/*|/var/folders/*|/private/var/folders/*) ;;
  *) echo "✗ 临时目录不在预期范围内：$WORK"; exit 2 ;;
esac
VICTIM_PIDS=""
cleanup_children() {
  local pid args rc=0
  # These are direct children still owned by this shell, never read app.pid.
  for pid in $VICTIM_PIDS; do
    if jobs -pr | grep -qx "$pid"; then
      /bin/kill -9 "$pid" 2>/dev/null || rc=1
    fi
    wait "$pid" 2>/dev/null || true
  done
  # S4 is started by the real package launcher. Its private fixture records
  # itself independently of the lifecycle PID record, which the test may delete.
  if [ -f "$WORK/s4/owned.pid" ]; then
    pid=$(cat "$WORK/s4/owned.pid") || return 1
    case "$pid" in ''|*[!0-9]*) return 1 ;; esac
    args=$(ps -p "$pid" -o args= 2>/dev/null) || args=""
    case "$args" in
      *"$WORK/s4/server/nas_companion_server.py --serve"*)
        /bin/kill -9 "$pid" 2>/dev/null || rc=1 ;;
      "") ;;
      *) echo "拒绝清理身份不符的测试进程" >&2; rc=1 ;;
    esac
  fi
  return "$rc"
}
cleanup() {
  local rc=$?
  trap - EXIT
  cleanup_children || rc=1
  rm -rf -- "$WORK" || rc=1
  exit "$rc"
}
trap cleanup EXIT

# ---------------------------------------------------------------- 桩与临时包
mkdir -p "$WORK/stub"
cat > "$WORK/stub/kill" <<'EOF'
#!/bin/sh
# 模拟"进程杀不掉"：只对 PID 文件里记着的那个 pid 说谎，其他 pid 也拒绝。
target=""
for a in "$@"; do
  case "$a" in -*) ;; *) target="$a" ;; esac
done
pid=""
[ -n "${STUB_PID_FILE:-}" ] && [ -f "$STUB_PID_FILE" ] && pid=$(cat "$STUB_PID_FILE" 2>/dev/null || true)
echo "kill $*" >> "${STUB_KILL_LOG:-/dev/null}"
if [ -n "$pid" ] && [ "$target" = "$pid" ]; then
  case "$1" in
    -0) exit 0 ;;                      # "还活着"
  esac
  exit 1                               # TERM/KILL 都被拒绝（等于 EPERM）
fi
exit 1 # No fallback may signal an unrelated process.
EOF
chmod +x "$WORK/stub/kill"
cat > "$WORK/stub/bashenv.sh" <<EOF
enable -n kill 2>/dev/null || true
PATH="$WORK/stub:\$PATH"
export PATH
EOF

setup_pkg() {   # $1=目录；$2=取旧版本（53e1bf8）
  local dir="$1" old="${2:-}"
  mkdir -p "$dir/server" "$dir/var" || return 1
  cp -R "$PKG/cmd" "$dir/cmd" || return 1
  cat > "$dir/server/nas_companion_server.py" <<'EOF' || return 1
# 测试替身：只负责"活着占着端口"，不监听任何东西。
import sys, time, os
if "--tls-status" in sys.argv:
    sys.exit(1)
if "--serve" in sys.argv:
    with open(os.environ["FNOS_TEST_OWNED_PID"], "w") as f:
        f.write(str(os.getpid()))
    time.sleep(120)
sys.exit(0)
EOF
  # 配置的真实位置是 $PKG_VAR/config.json（_common.sh 里 PKG_VAR=${TRIM_PKGVAR:-$TARGET/var}）；
  # 写到包根目录的话，两边根本没碰同一个文件，断言会"通过"得毫无意义。
  printf '{\n  "port": 8798,\n  "bind": "0.0.0.0",\n  "auth_mode": "pairing",\n  "docker_enabled": false,\n  "interval": 1.0,\n  "tls_enabled": true,\n  "hist_len": 300,\n  "devices": [{"id": "aa11", "name": "board"}]\n}\n' > "$dir/var/config.json" || return 1
  if [ -n "$old" ]; then
    local f
    for f in main uninstall_callback config_callback _common.sh; do
      git -C "$REPO" show "$BASE_REV:nas/fpk/nasscreencompanion/cmd/$f" > "$dir/cmd/$f" || return 1
      chmod +x "$dir/cmd/$f" || return 1
    done
  fi
  return 0
}

run_pkg() {   # $1=目录 $2=脚本 $3..=参数；用桩环境跑
  local dir="$1" script="$2"; shift 2
  FNOS_TEST_OWNED_PID="$dir/owned.pid" STUB_PID_FILE="$dir/var/app.pid" STUB_KILL_LOG="$dir/kill.log" \
  BASH_ENV="$WORK/stub/bashenv.sh" TRIM_APPDEST="$dir" TRIM_PKGVAR="$dir/var" \
  TRIM_TEMP_LOGFILE="$dir/temp.log" wizard_port=8899 wizard_interval=2 wizard_bind=lan \
  wizard_auth=pairing wizard_docker=false \
  bash "$dir/cmd/$script" "$@" 2>&1
}

start_victim() {
  sleep 3600 >/dev/null 2>&1 &
  VICTIM_PID=$!
  VICTIM_PIDS="$VICTIM_PIDS $VICTIM_PID"
}

# ---------------------------------------------------------------- S1 停不下来
S1="$WORK/s1"
setup_pkg "$S1"
start_victim; v1=$VICTIM_PID
printf '%s\n' "$v1" > "$S1/var/app.pid"
: > "$S1/app.sock"
if out=$(run_pkg "$S1" main stop); then rc=0; else rc=$?; fi
check "S1 stop 停不掉时返回非零" "$([ "$rc" != 0 ] && echo 0 || echo 1)" "rc=$rc"
check "S1 说明了 PID 记录被保留" "$(echo "$out" | grep -q 'PID 记录保留' && echo 0 || echo 1)"
check "S1 没有谎报已停止" "$(echo "$out" | grep -q '已停止' && echo 1 || echo 0)"
check "S1 输出里没有 shell 报错（\$VAR 紧跟多字节字符会在这里炸）" "$(echo "$out" | grep -q 'unbound variable' && echo 1 || echo 0)"
check "S1 PID 记录还在" "$([ -f "$S1/var/app.pid" ] && echo 0 || echo 1)"
check "S1 PID 记录内容没被改" "$([ "$(cat "$S1/var/app.pid" 2>/dev/null)" = "$v1" ] && echo 0 || echo 1)"
check "S1 socket 没被清掉" "$([ -e "$S1/app.sock" ] && echo 0 || echo 1)"

if sout=$(run_pkg "$S1" main status); then src=0; else src=$?; fi
check "S1 status 仍然看得到这个进程" "$(echo "$sout" | grep -q '进程存在' && echo 0 || echo 1)" "rc=$src"

if uout=$(run_pkg "$S1" uninstall_callback); then urc=0; else urc=$?; fi
check "S1 卸载在进程还活着时返回非零" "$([ "$urc" != 0 ] && echo 0 || echo 1)" "rc=$urc"
check "S1 卸载没有谎报'已释放端口与 Socket'" "$(echo "$uout" | grep -q '已释放端口' && echo 1 || echo 0)"
check "S1 卸载保住了 PID 记录" "$([ -f "$S1/var/app.pid" ] && echo 0 || echo 1)"
check "S1 被'释放'的进程其实还活着" "$(/bin/kill -0 "$v1" 2>/dev/null && echo 0 || echo 1)"

# 负控：53e1bf8 的旧脚本，同样的场景必须表现出假绿
S1O="$WORK/s1-old"
setup_pkg "$S1O" old || { echo "✗ 取不到 53e1bf8 的旧脚本"; exit 1; }
start_victim; v1o=$VICTIM_PID
printf '%s\n' "$v1o" > "$S1O/var/app.pid"
: > "$S1O/app.sock"
if uold=$(run_pkg "$S1O" uninstall_callback); then orc=0; else orc=$?; fi
check "负控：旧脚本在进程活着时仍 exit 0（假绿）" "$([ "$orc" = 0 ] && echo 0 || echo 1)" "rc=$orc"
check "负控：旧脚本确实报了'已释放端口与 Socket'" "$(echo "$uold" | grep -q '已释放端口' && echo 0 || echo 1)"
check "负控：旧脚本删掉了 PID 记录（于是再也查不到进程）" "$([ -f "$S1O/var/app.pid" ] && echo 1 || echo 0)"
check "负控：那个进程其实一直活着" "$(/bin/kill -0 "$v1o" 2>/dev/null && echo 0 || echo 1)"
/bin/kill -9 "$v1o" 2>/dev/null || true

# ---------------------------------------------------------------- S2 停不下来就别写配置
S2="$WORK/s2"
setup_pkg "$S2"
start_victim; v2=$VICTIM_PID
printf '%s\n' "$v2" > "$S2/var/app.pid"
check "S2 种子配置写在 \$PKG_VAR/config.json" "$([ -f "$S2/var/config.json" ] && echo 0 || echo 1)"
before=$(shasum -a 256 "$S2/var/config.json" | awk '{print $1}')
if cout=$(run_pkg "$S2" config_callback); then crc=0; else crc=$?; fi
after=$(shasum -a 256 "$S2/var/config.json" | awk '{print $1}')
check "S2 停止失败时配置提交返回非零" "$([ "$crc" != 0 ] && echo 0 || echo 1)" "rc=$crc"
check "S2 配置字节级没被改" "$([ "$before" = "$after" ] && echo 0 || echo 1)" "前 $before 后 $after"
check "S2 说明已放弃本次修改" "$(echo "$cout" | grep -q '已放弃本次设置修改' && echo 0 || echo 1)"
check "S2 没有报'设置已生效'" "$(echo "$cout" | grep -q '设置已生效' && echo 1 || echo 0)"
check "S2 失败时没有起第二个实例（端口没被占）" "$(echo "$cout" | grep -q '启动成功' && echo 1 || echo 0)"

# 负控：旧脚本会先把配置写下去（服务随后用内存里的旧值盖回来）
S2O="$WORK/s2-old"
setup_pkg "$S2O" old || exit 1
start_victim; v2o=$VICTIM_PID
printf '%s\n' "$v2o" > "$S2O/var/app.pid"
before_o=$(shasum -a 256 "$S2O/var/config.json" | awk '{print $1}')
run_pkg "$S2O" config_callback >/dev/null || true # old-script behavior is checked by bytes below
after_o=$(shasum -a 256 "$S2O/var/config.json" | awk '{print $1}')
check "负控：旧脚本在旧实例还活着时就把配置写下去了（正是 M25 的场景）" "$([ "$before_o" != "$after_o" ] && echo 0 || echo 1)"
/bin/kill -9 "$v2o" 2>/dev/null || true

# ---------------------------------------------------------------- S3 正常停止没被改坏
# 真实服务在退出前会写 $PKG_VAR/service-exit.json（app/server/nas_companion_server.py
# 的 main_with_exit_receipt）；stop 用它确认“已停且收尾成功”。夹具若只放一个裸 sleep，
# 进程被 TERM 杀掉后没有回执，stop 会如实报“收尾结果未通过”——那是 M21 的设计，
# 不是回归。这里让 victim 模拟真实服务：捕获 SIGTERM，写回执后再退出。
S3="$WORK/s3"
setup_pkg "$S3"
cat > "$S3/server/service_victim.py" <<'PY'
import json, os, signal, sys, time
target = os.environ["FNOS_TEST_EXIT_RECEIPT"]
ready = os.environ["FNOS_TEST_READY"]
stop = []
signal.signal(signal.SIGTERM, lambda *_: stop.append(1))
with open(ready, "w", encoding="utf-8") as stream:
    stream.write(str(os.getpid()))
while not stop:
    time.sleep(0.02)
with open(target, "w", encoding="utf-8") as stream:
    json.dump({"pid": os.getpid(), "code": 0}, stream)
PY
FNOS_TEST_EXIT_RECEIPT="$S3/var/service-exit.json" FNOS_TEST_READY="$S3/victim-ready" \
  python3 "$S3/server/service_victim.py" &
v3=$!
VICTIM_PIDS="$VICTIM_PIDS $v3"
for _ in $(seq 1 100); do [ -s "$S3/victim-ready" ] && break; sleep 0.05; done
printf '%s\n' "$v3" > "$S3/var/app.pid"
: > "$S3/app.sock"
if sout3=$(TRIM_APPDEST="$S3" TRIM_PKGVAR="$S3/var" bash "$S3/cmd/main" stop 2>&1); then rc3=0; else rc3=$?; fi
sleep 0.3
check "S3 正常停止仍然成功" "$([ "$rc3" = 0 ] && echo 0 || echo 1)" "rc=$rc3"
check "S3 正常停止后 PID 记录被清掉" "$([ -f "$S3/var/app.pid" ] && echo 1 || echo 0)"
check "S3 进程真的结束了" "$(/bin/kill -0 "$v3" 2>/dev/null && echo 1 || echo 0)"

# 负控：没有服务退出回执时，stop 必须报失败（进程已结束也不能谎报收尾成功）。
S3N="$WORK/s3-no-receipt"
setup_pkg "$S3N"
python3 -c 'import time; time.sleep(600)' &
v3n=$!
VICTIM_PIDS="$VICTIM_PIDS $v3n"
printf '%s\n' "$v3n" > "$S3N/var/app.pid"
: > "$S3N/app.sock"
if sout3n=$(TRIM_APPDEST="$S3N" TRIM_PKGVAR="$S3N/var" bash "$S3N/cmd/main" stop 2>&1); then rc3n=0; else rc3n=$?; fi
sleep 0.3
check "S3 负控：缺退出回执时 stop 返回非零" "$([ "$rc3n" != 0 ] && echo 0 || echo 1)" "rc=$rc3n"
check "S3 负控：明确说明收尾结果未通过" "$(echo "$sout3n" | grep -q '收尾结果未通过' && echo 0 || echo 1)"
check "S3 负控：进程仍然真的结束了" "$(/bin/kill -0 "$v3n" 2>/dev/null && echo 1 || echo 0)"

# ---------------------------------------------------------------- S4 启动失败要收尾
S4="$WORK/s4"
setup_pkg "$S4"
if aout=$(run_pkg "$S4" main start); then arc=0; else arc=$?; fi
check "S4 起来但没就绪时 start 返回非零" "$([ "$arc" != 0 ] && echo 0 || echo 1)" "rc=$arc"
check "S4 说了本次启动的进程没能结束" "$(echo "$aout" | grep -q '没能结束' && echo 0 || echo 1)"
check "S4 失败后 PID 记录被保留（下次还能看到它）" "$([ -f "$S4/var/app.pid" ] && echo 0 || echo 1)"
s4pid=$(cat "$S4/var/app.pid" 2>/dev/null || true)
if [ -n "$s4pid" ] && /bin/kill -0 "$s4pid" 2>/dev/null; then s4alive=0; else s4alive=1; fi
check "S4 记录里的进程确实还活着" "$s4alive" "pid=$s4pid"
 # S4 cleanup uses the independent fixture receipt and command identity.

if [ "$FAILED" != 0 ]; then
  echo "✗ $FAILED 项没通过"
  exit 1
fi
echo "PASS: 停止失败 → 记录保留 / 不写配置 / 不谎报已释放；正常停止与启动失败收尾都照旧"
exit 0
