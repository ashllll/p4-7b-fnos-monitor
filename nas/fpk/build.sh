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
find "$PKG" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true
find "$PKG" \( -name '*.pyc' -o -name '*.pyo' -o -name '.DS_Store' \) -delete 2>/dev/null || true

fnpack build --directory "$PKG"

FPK="$PKG.fpk"
SIZE=$(stat -f%z "$FPK" 2>/dev/null || stat -c%s "$FPK")
echo "产物：$(pwd)/${FPK}（$SIZE 字节）"

# 打完立刻验：包是拿去分发的，不能出现字节码缓存、macOS 垃圾、或本机标识
JUNK=$(tar tzf "$FPK"; tar xzOf "$FPK" app.tgz | tar tz) || true
BAD=$(echo "$JUNK" | grep -E '__pycache__|\.py[co]$|\.DS_Store|^\._' || true)
if [ -n "$BAD" ]; then
  echo "✗ 包里混进了不该有的东西：" >&2
  echo "$BAD" | sed 's/^/    /' >&2
  echo "  （打之前先清干净；这个检查就是为它准备的）" >&2
  exit 1
fi
echo "✓ 包内没有字节码缓存 / .DS_Store / ._ 资源分叉"
