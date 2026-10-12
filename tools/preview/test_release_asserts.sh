#!/usr/bin/env bash
# 回归：主机预览的验收断言必须在**默认（Release）构建**里真的执行（审计 M17）
#
# 背景：这个可执行文件的 assert() 就是判据本身。默认构建类型是 Release（CMakeLists.txt:6-8），
# CMake 于是给每个源文件加 -DNDEBUG —— assert 被预处理成空语句，验收照样打印 PASS、
# 退出码照样 0，而它什么都没验。更糟的是当时还有 `assert(fread(...))` / `assert(fnos_status_parse(...))`
# 这种**带副作用的 assert**：NDEBUG 下连"读夹具"和"解析夹具"都没发生，后面按 fixture.nvols
# 遍历自然 0 次，一条永远为真的断言。
#
# 本回归查三件事（全部在临时目录里，不碰真机、不改仓库产物）：
#   1) 用默认 Release 配置去 configure，读 preview target 真实的 C_FLAGS：
#      必须同时有 -DNDEBUG 与 -UNDEBUG，且 -UNDEBUG 在**后面**（后出现的宏定义生效）。
#   2) 用同一份 flags 编一个 assert(0) 探针：带尾部 -UNDEBUG 必须真的 abort（rc≠0），
#      把 -UNDEBUG 去掉必须静默返回 0 —— 这是"这条门能抓住 NDEBUG 回归"的负向对照。
#   3) 静态扫 preview.c：assert( 里不许再出现有副作用的调用（读/解析/分配）。
#   4) 已有产物时：二进制里必须还引用 __assert_rtn（断言没被优化掉），并且喂一个
#      截断/空夹具必须**非零退出 + FAIL 文案**，而不是 PASS。
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT
fail=0
incomplete=0
bad() { printf '  ✗ %s\n' "$1"; fail=1; }

# ── 1. 默认 Release 配置下 preview target 的真实编译标志 ────────────────
if cmake -S tools/preview -B "$TMP/rel" -DCMAKE_BUILD_TYPE=Release -DFNOS_PREVIEW_SANITIZE=OFF \
        >"$TMP/cmake.log" 2>&1; then
  FLAGS_FILE="$TMP/rel/CMakeFiles/preview.dir/flags.make"
  if [ ! -f "$FLAGS_FILE" ]; then
    bad "configure 没生成 $FLAGS_FILE"
  else
    python3 - "$FLAGS_FILE" <<'PY' || fail=1
import io, re, sys
text = io.open(sys.argv[1], encoding="utf-8", errors="replace").read()
line = next((l for l in text.splitlines() if l.startswith("C_FLAGS")), "")
if not line:
    print("  ✗ flags.make 里没有 C_FLAGS"); raise SystemExit(1)
if "-DNDEBUG" not in line:
    print("  ✗ Release 配置里居然没有 -DNDEBUG —— 这个回归的前提变了，先核对 CMakeLists"); raise SystemExit(1)
if "-UNDEBUG" not in line:
    print("  ✗ preview target 的 C_FLAGS 里没有 -UNDEBUG：Release 下所有验收断言都会被丢掉"); raise SystemExit(1)
if line.rfind("-UNDEBUG") < line.rfind("-DNDEBUG"):
    print("  ✗ -UNDEBUG 出现在 -DNDEBUG 之前：后面的宏定义才生效，断言仍然会被丢掉"); raise SystemExit(1)
print("  ✓ Release 下 -DNDEBUG 之后跟着 -UNDEBUG（断言保留）")
PY
  fi
else
  bad "cmake configure 失败（见下方日志）"; tail -5 "$TMP/cmake.log"
fi

# ── 2. 负向对照：同一份 flags，去掉 -UNDEBUG 后 assert(0) 就消失了 ────────
cat >"$TMP/probe.c" <<'C'
#include <assert.h>
int main(void) { assert(0 && "probe must abort when asserts are live"); return 0; }
C
CFLAGS_LINE=$(grep -m1 -E '^C_FLAGS[A-Za-z0-9_]* = ' "$TMP/rel/CMakeFiles/preview.dir/flags.make" 2>/dev/null || true)
CFLAGS_LINE=${CFLAGS_LINE#*= }
if [ -n "$CFLAGS_LINE" ]; then
  # shellcheck disable=SC2086
  cc $CFLAGS_LINE -o "$TMP/probe_live" "$TMP/probe.c" 2>"$TMP/probe_live.log" || bad "探针编译失败（-UNDEBUG）"
  # shellcheck disable=SC2086
  cc ${CFLAGS_LINE//-UNDEBUG/} -o "$TMP/probe_dead" "$TMP/probe.c" 2>"$TMP/probe_dead.log" || bad "探针编译失败（无 -UNDEBUG）"
  if [ -x "$TMP/probe_live" ]; then
    "$TMP/probe_live" >/dev/null 2>&1
    [ $? -ne 0 ] || bad "带 -UNDEBUG 的 assert(0) 没有 abort —— 断言根本没生效"
  fi
  if [ -x "$TMP/probe_dead" ]; then
    "$TMP/probe_dead" >/dev/null 2>&1
    [ $? -eq 0 ] || bad "去掉 -UNDEBUG 后 assert(0) 仍然终止进程 —— 负向对照失效，这条门抓不到 NDEBUG 回归"
  fi
  [ "$fail" = 0 ] && printf '  ✓ 负向对照：-UNDEBUG 生效时 abort，去掉后静默通过\n'
else
  bad "拿不到 C_FLAGS，负向对照没跑"
fi

# ── 3. assert 里不许有副作用（读/解析/分配） ─────────────────────────────
#    注释里会引用这两条旧写法，所以先剥掉 C 注释再扫 —— 否则这条门只会打到自己的文档。
python3 - tools/preview/preview.c >"$TMP/side.log" 2>&1 <<'PY'
import io, re, sys
src = io.open(sys.argv[1], encoding="utf-8", errors="replace").read()
src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
src = re.sub(r"//[^\n]*", " ", src)
pat = re.compile(r"assert\s*\([^;]*?\b(fread|fgets|fscanf|fopen|fclose|fnos_status_parse|malloc|calloc|realloc|strdup|memcpy|snprintf)\s*\(")
hits = []
for n, line in enumerate(src.splitlines(), 1):
    if pat.search(line):
        hits.append(f"{sys.argv[1]}:{n}: {line.strip()}")
if hits:
    print("\n".join(hits))
raise SystemExit(1 if hits else 0)
PY
side_rc=$?
if [ "$side_rc" -ne 0 ]; then
  bad "assert 扫描失败（rc=${side_rc}）"
fi
if [ -s "$TMP/side.log" ]; then
  bad "preview.c 里又有带副作用的 assert（NDEBUG 下副作用一起消失）："
  sed 's/^/      /' "$TMP/side.log"
elif [ "$side_rc" -eq 0 ]; then
  printf '  ✓ preview.c 的 assert 里没有读/解析/分配这类副作用\n'
fi

# ── 4. 产物证据：断言真的编进去了，且坏夹具是硬失败 ──────────────────────
PREVIEW_ENV_ARGS=(env)
while IFS= read -r name; do
  case "$name" in PREVIEW_*) PREVIEW_ENV_ARGS+=(-u "$name") ;; esac
done < <(compgen -e)
BIN=tools/preview/build/preview
if [ -x "$BIN" ]; then
  if nm -u "$BIN" 2>/dev/null | grep -q "__assert_rtn\|__assert_fail"; then
    printf '  ✓ 产物里仍引用 assert 失败处理（断言没被优化掉）\n'
  else
    bad "产物里找不到 __assert_rtn/__assert_fail：这次构建把断言全丢了，先重跑 bash tools/preview/run.sh"
  fi
  mkdir -p "$TMP/out"
  printf '{"ready":true,' >"$TMP/truncated.json"
  "${PREVIEW_ENV_ARGS[@]}" PREVIEW_HARDWARE_FIXTURE="$TMP/truncated.json" "$BIN" "$TMP/out" >"$TMP/trunc.log" 2>&1
  rc=$?
  if [ "$rc" -eq 0 ]; then
    bad "截断的硬件夹具居然让预览退出 0（应该非零退出）"
  elif ! grep -q "FAIL: 硬件夹具" "$TMP/trunc.log"; then
    bad "截断夹具没有打出 FAIL: 硬件夹具 文案：$(tail -1 "$TMP/trunc.log")"
  else
    printf '  ✓ 截断夹具 → rc=%s + 「%s」\n' "$rc" "$(grep -m1 'FAIL: 硬件夹具' "$TMP/trunc.log")"
  fi
  : >"$TMP/empty.json"
  "${PREVIEW_ENV_ARGS[@]}" PREVIEW_HARDWARE_FIXTURE="$TMP/empty.json" "$BIN" "$TMP/out" >"$TMP/empty.log" 2>&1
  rc=$?
  if [ "$rc" -eq 0 ] || ! grep -q "FAIL: 硬件夹具是空的" "$TMP/empty.log"; then
    bad "空夹具没有被拒绝（rc=${rc}，日志：$(tail -1 "$TMP/empty.log")）"
  else
    printf '  ✓ 空夹具 → rc=%s + 空夹具拒绝文案\n' "$rc"
  fi
else
  incomplete=1
  printf '  ! 没有 %s（先跑 bash tools/preview/run.sh），产物侧证据本轮未完成\n' "$BIN"
fi

if [ "$fail" = 0 ] && [ "$incomplete" != 0 ]; then
  echo "INCOMPLETE: 缺少预览产物，断言门未完成"
  exit 2
fi
if [ "$fail" = 0 ]; then
  echo "PASS: Release/NDEBUG 下验收断言仍然生效；夹具读取与解析是显式硬失败，不再藏在 assert 里"
else
  echo "FAIL: Release/NDEBUG 断言回归未通过（见上面 ✗）"
fi
exit "$fail"
