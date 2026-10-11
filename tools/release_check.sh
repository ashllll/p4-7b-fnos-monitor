#!/bin/bash
# 通用发布构建自检（P3 验收里的"秘密扫描"那一条，针对固件侧）。
#
# 做两件事：
#   1. 把本地真实配置 components/fnos_monitor/fnos_config.h 暂时挪开，另建 build-release/
#      编一版 —— 证明"不含个人配置的通用构建"确实编得出来（历史上有文件直接
#      #include <fnos_config.h>，没有本地配置就编不过）。
#   2. 用本地真实配置里的值去搜产物，确认 Wi-Fi 口令、令牌、NAS 地址一个都没进去。
#
# 用法：bash tools/release_check.sh
# 注意：会新建/覆盖 build-release/，不动 build/。真实配置一定会被放回去（trap）。
set -euo pipefail
cd "$(cd "$(dirname "$0")/.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}"

CFG=components/fnos_monitor/fnos_config.h
STASH="$CFG.release-check"
MOVED=0
restore() {
  local rc=$?
  trap - EXIT
  if [ "$MOVED" = 1 ]; then
    if [ -e "$CFG" ] || [ -L "$CFG" ] || ! mv "$STASH" "$CFG"; then
      echo "真实配置恢复失败：保留 ${STASH}，请手动恢复。" >&2
      rc=1
    fi
  fi
  exit "$rc"
}
trap restore EXIT
[ ! -e "$STASH" ] && [ ! -L "$STASH" ] || { echo "配置暂存路径已存在，停止构建。" >&2; exit 1; }
if [ -e "$CFG" ] || [ -L "$CFG" ]; then
  [ -f "$CFG" ] && [ ! -L "$CFG" ] || { echo "真实配置不是普通文件。" >&2; exit 1; }
  mv "$CFG" "$STASH" || { echo "无法挪开真实配置，停止构建。" >&2; exit 1; }
  MOVED=1
  [ -f "$STASH" ] && [ ! -e "$CFG" ] || { echo "真实配置隔离失败。" >&2; exit 1; }
  echo "== 已挪开 ${CFG}，开始通用构建 =="
else
  echo "没有 ${CFG}：这一版本来就是通用配置，直接构建即可。"
fi

rm -rf build-release
# pipefail 将构建失败传给整个管道，不能仅接受 tail 的成功退出码。
if ! bash ./idf.sh -B build-release build 2>&1 | tail -3; then
  echo "构建失败：通用构建必须能在没有本地配置的情况下编出来。"
  exit 1
fi

echo "== 扫产物里的个人数据 =="
python3 - "$STASH" <<'PY'
import io, os, re, sys
stash = sys.argv[1]
if not os.path.exists('build-release/fnos_monitor.bin'):
    print('产物不存在，构建没成功'); raise SystemExit(1)
b = io.open('build-release/fnos_monitor.bin', 'rb').read()
if not os.path.exists(stash):
    print('（本机没有真实配置可对照，只报大小）', len(b), '字节'); raise SystemExit(0)
cfg = io.open(stash, encoding='utf-8').read()
example_path = 'components/fnos_monitor/fnos_config.example.h'
example = io.open(example_path, encoding='utf-8').read() if os.path.isfile(example_path) else ''
def val(k, source=cfg):
    m = re.search(r'#define\s+%s\s+"([^"]*)"' % k, source)
    return m.group(1) if m else None
bad = 0
compared = 0
for k in ('APP_WIFI_SSID', 'APP_WIFI_PASS', 'FNOS_TOKEN', 'FNOS_HOST'):
    v = val(k)
    if v is None:
        print(f"  {k:16} 无法读取配置值，不能完成扫描"); bad += 1
    if v is not None and v == val(k, example):
        print(f"  {k:16} 与公开模板相同，未作真实值对照")
        continue
    compared += bool(v)
    hit = bool(v) and v.encode() in b
    print(f'  {k:16} {"命中 ← 不合格" if hit else "未出现"}')
    bad += bool(hit)
print(f'  产物 {len(b)} 字节 —— ' + (f'{compared} 个非模板真实值未命中 ✓' if bad == 0 else f'{bad} 项不合格 ✗'))
raise SystemExit(1 if bad else 0)
PY
