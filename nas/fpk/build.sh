#!/bin/bash
# 打 .fpk 的唯一正确入口。**别直接跑 `fnpack build`。**
#
# 为什么：`fnpack build` 会把包目录里的**一切**塞进 app.tgz。开发机上一旦有人
# 用源码树里的脚本跑过服务端（`nas_companion_server.py` 顶部 `from fnos_collector
# import …`），Python 就会在 `app/server/__pycache__/` 留下一份 .pyc——那是**本机
# Python 3.14 的字节码**，而 NAS 上跑的是 python312。直接打出来的包会凭空多出
# 60 KB 的无用字节码，而且这东西不进任何断言（测试套件会先清理再构建，所以它
# 永远是绿的，被漏掉的恰恰是**你手上那个 .fpk**）。
#
# 所以：清理 + 构建 + 立刻验一遍产物，三步一起做。
set -euo pipefail
cd "$(cd "$(dirname "$0")" && pwd)"
export PATH="$HOME/.local/bin:$PATH"

PKG=nasscreencompanion
python3 ../sync_docker_metrics.py
# Avoid recursive rm: remove bytecode files first, then the now-empty cache directories.
find "$PKG" -type f \( -name '*.pyc' -o -name '*.pyo' -o -name '.DS_Store' \) -exec rm -f {} +
find "$PKG" -depth -type d -name '__pycache__' -exec rmdir {} +

fnpack build --directory "$PKG"

FPK="$PKG.fpk"
SIZE=$(stat -f%z "$FPK" 2>/dev/null || stat -c%s "$FPK")
echo "产物：$(pwd)/${FPK}（$SIZE 字节）"

# 打完立刻验：包是拿去分发的，不能出现字节码缓存、macOS 垃圾、或本机标识
OUTER=$(tar tzf "$FPK")
# 取 app.tgz 的清单要单独判成败（审计 M22）：以前整条挂在 `|| true` 后面，
# 解包一失败 JUNK 就是空的，grep 什么也找不到，于是照样打出"✓ 包内干净"——
# 检查失败被当成了检查通过。包结构不对就该当场红，而不是给一个假的绿色。
if ! APP=$(tar xzOf "$FPK" app.tgz | tar tz); then
  echo "✗ 读不出包内的 app.tgz（$FPK 结构不对或已损坏），包内清单无法核对" >&2
  exit 1
fi
JUNK=$(printf '%s\n%s\n' "$OUTER" "$APP")
if BAD=$(printf '%s\n' "$JUNK" | grep -E '__pycache__|\.py[co]$|\.DS_Store|(^|/)\._'); then
  :
else
  grep_rc=$?
  if [ "$grep_rc" -ne 1 ]; then
    echo "✗ 包内清单检查工具失败，拒绝放行" >&2
    exit 1
  fi
fi
if [ -n "$BAD" ]; then
  echo "✗ 包里混进了不该有的东西：" >&2
  echo "$BAD" | sed 's/^/    /' >&2
  echo "  （打之前先清干净；这个检查就是为它准备的）" >&2
  exit 1
fi
echo "✓ 包内没有字节码缓存 / .DS_Store / ._ 资源分叉"

# Package identity has one source: the completed archive, checked against its source manifest.
python3 release_metadata.py --write
