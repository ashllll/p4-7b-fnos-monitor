#!/bin/bash
# 审计 M36/M57 回归：输出目录不许删仓库外的数据；验收阶段不许继承调用方的 PREVIEW_* 分支变量。
#
# M57：run.sh 会 `rm -rf "$OUT"`，而 OUT 来自位置参数。旧脚本只挡了 ".." 和绝对路径，
#       `guard-link/sub`（guard-link 是指向仓库外的符号链接）里没有 ".."，删掉的却是
#       链接那头的目录。这里用**真哨兵目录**证明它现在会被拒绝且哨兵毫发无损。
# M36：53e1bf8 的脚本一个变量都不清，调用方 shell 里残留的分支 PREVIEW_* 会一路漏进
#       硬件矩阵/OOM 阶段——那些阶段只看退出码，于是"跑了子集"照样打印 PASS。
#       这里把全部分支都设成脏值跑一遍，并在真二进制外面套一层记录器，逐个阶段核对
#       真正传进去的 PREVIEW_* 只有该阶段自己设的那几个。
#
# 记录器的做法：run.sh 里 `cmake --build` 会重新链接 build/preview（pair_msgs.h 每次重生成，
# 二进制一定比它旧），所以不能把记录器放在 build/preview 上——会被链接覆盖。改成用
# `sed` 从**同一个 run.sh** 生成一份只把可执行文件路径换成记录器的副本（改动行数是要断言的），
# 副本里清环境的逻辑与正式脚本逐字相同。
#
# 需要先跑过一次 tools/preview/run.sh（要有 build/preview 和 build/config/sdkconfig.h）。
set -u

PREVIEW_DIR=$(cd "$(dirname "$0")" && pwd -P)
REPO=$(cd "$PREVIEW_DIR/../.." && pwd)
BASE_REV=53e1bf868bee0e2b66b05a8df8df342ba6ee29a4
FAILED=0
INCOMPLETE=0
QUICK="${GUARD_QUICK:-0}"     # GUARD_QUICK=1 只跑秒级的参数/静态检查

say() { echo "[preview-guard] $*"; }
check() {   # $1=名字 $2=0/1 $3=说明
  if [ "$2" = 0 ]; then echo "✓ $1${3:+　—— $3}"
  else echo "✗ $1${3:+　—— $3}"; FAILED=$((FAILED + 1)); fi
}

WORK=$(mktemp -d) || exit 2
case "$WORK" in
  /tmp/*|/var/folders/*|/private/var/folders/*) ;;
  *) echo "✗ 临时目录不在预期范围内：$WORK"; exit 2 ;;
esac
OWN=""; PROBE=""; GUARDBIN=""
cleanup() {
  local rc=$?
  [ -z "$PROBE" ] || rm -f -- "$PROBE" || rc=1
  [ -z "$GUARDBIN" ] || rm -f -- "$GUARDBIN" || rc=1
  [ -z "$OWN" ] || rm -rf -- "$OWN" || rc=1
  rm -rf -- "$WORK" || rc=1
  exit "$rc"
}
trap cleanup EXIT
OWN=$(mktemp -d "$PREVIEW_DIR/.guard-work.XXXXXX") || exit 2
LINK="$OWN/link"
OUTDIR="$OWN/out"

# ---------------------------------------------------------------- M57 参数守卫
guard_case() {   # $1=说明 $2=期望 rc $3..=参数
  local desc="$1" want="$2"; shift 2
  local out rc
  out=$(cd "$PREVIEW_DIR" && bash ./run.sh "$@" 2>&1); rc=$?
  check "$desc" "$([ "$rc" = "$want" ] && echo 0 || echo 1)" "rc=${rc}（期望 ${want}）$(echo "$out" | head -1)"
}

guard_case "绝对路径被拒绝" 2 /tmp/guard-should-not-exist
guard_case ".. 被拒绝" 2 ..
guard_case "../.. 被拒绝" 2 ../..
guard_case "自身目录被拒绝" 2 .
guard_case "藏在中间 .. 的路径被拒绝" 2 out/../../guard-should-not-exist
guard_case "绝对路径 / 被拒绝" 2 /

# 符号链接逃逸：guard-link 指向仓库外，哨兵必须在 rm -rf 之后仍然完整
SENTINEL="$WORK/sentinel"
mkdir -p "$SENTINEL/sub"
echo keep > "$SENTINEL/keep.txt"
echo keep > "$SENTINEL/sub/keep2.txt"
ln -sfn "$SENTINEL" "$LINK"
guard_case "符号链接指向仓库外时被拒绝" 2 "${LINK#"$PREVIEW_DIR"/}/sub"
check "哨兵目录还在" "$([ -d "$SENTINEL" ] && echo 0 || echo 1)"
check "哨兵文件还在（内容没动）" "$([ "$(cat "$SENTINEL/keep.txt" 2>/dev/null)" = keep ] && echo 0 || echo 1)"
check "哨兵子目录还在" "$([ "$(cat "$SENTINEL/sub/keep2.txt" 2>/dev/null)" = keep ] && echo 0 || echo 1)"
rm -f "$LINK"

# ---------------------------------------------------------------- M36 静态负控
# 旧脚本清了哪些、漏了哪些：直接读 53e1bf8 的原件，不靠回忆。
OLD_CLEARED=$(git -C "$REPO" show "$BASE_REV:tools/preview/run.sh" \
  | grep -o '\-u [A-Z_]*' | sed 's/-u //' | sort -u | tr '\n' ' ')
ALL_VARS=$(grep -o 'getenv("PREVIEW_[A-Z_]*"' "$PREVIEW_DIR/preview.c" | sed 's/.*"\(.*\)"/\1/' | sort -u | tr '\n' ' ')
LEAKED=""
for v in $ALL_VARS; do
  case " $OLD_CLEARED " in *" $v "*) ;; *) LEAKED="$LEAKED $v" ;; esac
done
check "静态负控：53e1bf8 的脚本完全没有隔离（全部 $(echo $ALL_VARS | wc -w | tr -d ' ') 个变量都能从调用方漏进来）" \
  "$([ -n "$(echo $LEAKED)" ] && echo 0 || echo 1)" "可漏：$(echo $LEAKED | tr -s ' ')"
check "静态负控：现在脚本声明覆盖 preview.c 的全部变量" "$(grep -q 'PREVIEW_BRANCH_VARS' "$PREVIEW_DIR/run.sh" && echo 0 || echo 1)" "共 $(echo $ALL_VARS | wc -w | tr -d ' ') 个"

if [ "$QUICK" = 1 ]; then
  say "GUARD_QUICK=1：跳过整跑 preview 的脏环境用例"
  INCOMPLETE=1
elif [ ! -x "$PREVIEW_DIR/build/preview" ]; then
  say "缺少 build/preview：本轮未完成"
  INCOMPLETE=1
else
  # 每条 `./build/preview` 都要被换成记录器；行数对不上说明脚本结构变了，断言会失败
  # 文本里 9 处调用点，运行时 29 次调用（含刷新、硬件矩阵、动效与八套主题）；
  # 运行时次数由下面的 env.log 块数断言，这里只钉住"改的地方就这几行"。
  PROBE=$(mktemp "$PREVIEW_DIR/.run-guard-probe.XXXXXX") || exit 2
  GUARDBIN=$(mktemp "$PREVIEW_DIR/build/guard-preview.XXXXXX") || exit 2
  CALLS=$(grep -o '\./build/preview' "$PREVIEW_DIR/run.sh" | wc -l | tr -d ' ')
  LINES=$(grep -c '\./build/preview' "$PREVIEW_DIR/run.sh" | tr -d ' ')
  sed "s#\./build/preview#./build/$(basename "$GUARDBIN")#g" "$PREVIEW_DIR/run.sh" > "$PROBE"
  CHANGED=$(diff "$PREVIEW_DIR/run.sh" "$PROBE" | grep -c '^[<>]')
  check "探针副本只改了 preview 可执行文件路径" \
    "$([ "$CALLS" = 9 ] && [ "$LINES" = 9 ] && [ "$CHANGED" = 18 ] && echo 0 || echo 1)" \
    "调用点 $CALLS 处（$LINES 行）、diff $CHANGED 行（期望 9 处 / 18 行）"
  cat > "$GUARDBIN" <<'EOF'
#!/bin/bash
# 记录本阶段真正拿到的 PREVIEW_* 环境，再转交真二进制。
{
  printf 'ARGV %s\n' "$*"
  env | grep '^PREVIEW_' | sort | sed 's/^/ENV /'
  echo "--"
} >> "${GUARD_ENV_LOG:-/dev/null}"
exec "$(dirname "$0")/preview" "$@"
EOF
  chmod +x "$GUARDBIN" || exit 2

  # 脏环境：全部分支变量都设成"漏进去就会改路径"的值
  ENVLOG="$WORK/env.log"
  : > "$ENVLOG"
  HOSTILE=(
    PREVIEW_CHART_MAP=1 PREVIEW_MOTION=1 PREVIEW_NUMBERS=1 PREVIEW_CARDMAP=1 PREVIEW_DATA_MOTION=1
    PREVIEW_BARMAP=/nonexistent/barmap.txt
    PREVIEW_PAIR_FIRST_ENTRY=1 PREVIEW_ALLOC_FAIL_AT=0 PREVIEW_ALLOC_SEED_ONE=1
    PREVIEW_AUDIT_ALL=1 PREVIEW_BANDS_AT=1 PREVIEW_CARDLIST=1 PREVIEW_CARDMAP_AT=1
    PREVIEW_CARD_OVERLAP=1 PREVIEW_ENTER_MID=1 PREVIEW_HARDWARE_FIXTURE=/nonexistent/lost.json
    PREVIEW_MOTION_FIRST_ENTRY=1 PREVIEW_MOTION_INPUT=1 PREVIEW_MOTION_REPEAT_TARGET=1
    PREVIEW_MOTION_RESPONSIVENESS=1 PREVIEW_MOTION_SCROLL=1 PREVIEW_OVERLAP_TRACE=1
    PREVIEW_PAGE_CARDS=1 PREVIEW_PARTIAL=1 PREVIEW_PROBE=1 PREVIEW_SIZE=1
    PREVIEW_TRACE_CARDS=1 PREVIEW_WHO=1 PREVIEW_WHOALL=1
  )
  (cd "$PREVIEW_DIR" && env "${HOSTILE[@]}" GUARD_ENV_LOG="$ENVLOG" \
    bash "$PROBE" "${OUTDIR#"$PREVIEW_DIR"/}") > "$WORK/dirty.log" 2>&1
  drc=$?
  check "脏环境下仍然成功跑完验收" "$([ "$drc" = 0 ] && echo 0 || echo 1)" "rc=$drc $(grep -m1 '渲染帧数' "$WORK/dirty.log" || true)"
  frames=$(grep -m1 '渲染帧数' "$WORK/dirty.log" | grep -o '[0-9]\+' || echo 0)
  check "脏环境下渲染了完整样本（≥84 帧，没跑成子集）" "$([ "${frames:-0}" -ge 84 ] && echo 0 || echo 1)" "帧数=$frames"
  check "脏环境下硬件矩阵与 OOM 阶段都报了 PASS" \
    "$([ "$(grep -c '^hardware inventory .*: PASS' "$WORK/dirty.log")" = 5 ] && echo 0 || echo 1)" \
    "$(grep -c '^hardware inventory .*: PASS' "$WORK/dirty.log") 条 PASS"

  # 逐阶段核对：每个阶段允许出现的 PREVIEW_* 只有它自己设的那几个
  blocks=$(grep -c '^--$' "$ENVLOG")
  check "记录到了 29 个 preview 阶段（含刷新、矩阵、动效与主题）" "$([ "$blocks" = 29 ] && echo 0 || echo 1)" "记录到 $blocks"
  allowed_for() {
    case "$1" in
      1) echo "PREVIEW_CHART_MAP" ;;
      2) echo "PREVIEW_BARMAP" ;;
      3) echo "PREVIEW_PARTIAL PREVIEW_REFRESH_WORK" ;;
      4|5|6|7) echo "PREVIEW_HARDWARE_FIXTURE" ;;
      8) echo "PREVIEW_ALLOC_FAIL_AT PREVIEW_HARDWARE_FIXTURE" ;;
      13) echo "PREVIEW_PARTIAL PREVIEW_MOTION PREVIEW_MOTION_INPUT PREVIEW_MOTION_FIRST_ENTRY PREVIEW_MOTION_REPEAT_TARGET PREVIEW_MOTION_RESPONSIVENESS" ;;
      9|10|11|12|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29) echo "PREVIEW_DATA_MOTION" ;;
      *) echo "UNEXPECTED_STAGE" ;;
    esac
  }
  i=0
  bad=""
  while IFS= read -r line; do
    case "$line" in
      '--') ;;
      ARGV*) i=$((i + 1)) ;;          # 每块的 ARGV 是第一行：用它编号，块内容才不会错位
      ENV\ *) v=${line#ENV }; name=${v%%=*}
             case " $(allowed_for "$i") " in *" $name "*) ;; *) bad="$bad 阶段$i:$name" ;; esac ;;
    esac
  done < "$ENVLOG"
  check "没有阶段拿到调用方残留的分支变量" "$([ -z "$bad" ] && echo 0 || echo 1)" "${bad:-无}"
fi

if [ "$FAILED" != 0 ]; then
  echo "✗ $FAILED 项没通过"
  exit 1
fi
if [ "$INCOMPLETE" != 0 ]; then
  echo "INCOMPLETE: 未执行完整预览环境隔离回归"
  exit 2
fi
echo "PASS: 输出目录守卫拒绝仓库外路径且不删数据；全部分支变量按阶段隔离"
exit 0
