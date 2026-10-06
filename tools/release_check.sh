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
set -u
cd "$(cd "$(dirname "$0")/.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}"

CFG=components/fnos_monitor/fnos_config.h
STASH="$CFG.release-check"
restore() { [ -f "$STASH" ] && mv -f "$STASH" "$CFG"; }
trap restore EXIT

if [ ! -f "$CFG" ]; then
  echo "没有 ${CFG}：这一版本来就是通用配置，直接构建即可。"
else
  mv -f "$CFG" "$STASH"
  echo "== 已挪开 ${CFG}，开始通用构建 =="
fi

rm -rf build-release
# 注意：管道后面的退出码是 tail 的，构建失败也会是 0 —— 必须取 PIPESTATUS[0]。
bash ./idf.sh -B build-release build 2>&1 | tail -3
if [ "${PIPESTATUS[0]}" != "0" ]; then
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
def val(k):
    m = re.search(r'#define\s+%s\s+"([^"]*)"' % k, cfg)
    return m.group(1) if m else None
bad = 0
for k in ('APP_WIFI_SSID', 'APP_WIFI_PASS', 'FNOS_TOKEN', 'FNOS_HOST'):
    v = val(k)
    hit = bool(v) and len(v) > 3 and v.encode() in b
    print(f'  {k:16} {"命中 ← 不合格" if hit else "未出现"}')
    bad += bool(hit)
print(f'  产物 {len(b)} 字节 —— ' + ('不含个人数据 ✓' if bad == 0 else f'{bad} 项不合格 ✗'))
raise SystemExit(1 if bad else 0)
PY
