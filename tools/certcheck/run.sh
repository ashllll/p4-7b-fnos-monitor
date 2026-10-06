#!/bin/bash
# 用**固件里那个真实的 `pem_fingerprint()`** 算一遍证书指纹，与 openssl 对照。
#
# 为什么值得单独做：指纹是整套信任模型里唯一的人工锚点 —— 用户在 NAS 管理页看到
# 一串、在板子屏幕上看一串，两串一样才放行。**这两个值是由两段完全独立的代码算出来的**：
# 服务端是纯 Python（`pem_fingerprint()`：PEM 第一段证书的 base64 正文 → sha256），
# 板端是 `components/fnos_monitor/fnos_pair.c` 里的 C 版（mbedtls_base64_decode +
# mbedtls_sha256 + 大写冒号分隔）。两边算法只要有一处不一样（比如板端把整条证书链
# 的 base64 全拼起来、或者没处理 \r\n），用户就会看到两个不同的值，而且**怎么都对不上**，
# 现场完全无从判断是证书问题还是代码问题。
#
# 做法上的取舍：为了让"测的确实是固件那份代码"，这里**不抄一份函数**，而是从
# `fnos_pair.c` 里把 `pem_fingerprint` 原文抠出来编译（抠出来的文本同时校验，改一行
# 就会失败提醒）。mbedtls 用 ESP-IDF 自带的源码编到主机上，不写替身——
# 这样连 base64/sha256 的实现都跟板子上是同一份。
set -u
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cd "$(cd "$(dirname "$0")/../.." && pwd)"

SRC=components/fnos_monitor/fnos_pair.c
MB="$HOME/.platformio/packages/framework-espidf@3.50503.0/components/mbedtls/mbedtls"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ── 1. 从固件源码里抠出真实函数
python3 tools/certcheck/extract.py "$SRC" "$OUT/fp.c" || exit 1

# ── 2. 拿一份真实证书（优先用测试实例生成的，没有就用临时自签的）
PEM="$OUT/server.crt"
if [ -f /tmp/nsc-soak/var/tls/server.crt ]; then
  cp /tmp/nsc-soak/var/tls/server.crt "$PEM"
  echo "用本地测试实例的证书：/tmp/nsc-soak/var/tls/server.crt"
else
  openssl req -x509 -newkey rsa:2048 -nodes -keyout "$OUT/k.pem" -out "$PEM" \
          -days 1 -subj "/CN=certcheck" >/dev/null 2>&1
  echo "用临时自签证书（没有本地测试实例）"
fi

# ── 3. 编 mbedtls 的两个源文件到主机（不写替身）
# platform_util.c 也要带上：sha256.c 会调 mbedtls_platform_zeroize，
# 少了它链接期才报错（Undefined symbols: _mbedtls_platform_zeroize）
for f in base64 sha256 platform_util; do
  cc -c -I"$MB/include" -o "$OUT/$f.o" "$MB/library/$f.c" || exit 1
done
cc -I"$MB/include" -o "$OUT/fp" tools/certcheck/main.c "$OUT/fp.c" \
   "$OUT/base64.o" "$OUT/sha256.o" "$OUT/platform_util.o" 2>&1 | head -20
[ -x "$OUT/fp" ] || { echo "编译失败"; exit 1; }

# ── 4. 三方对照（指纹 + CN/到期）
FIRMWARE=$("$OUT/fp" "$PEM")
OPENSSL=$(openssl x509 -in "$PEM" -noout -fingerprint -sha256 | sed 's/.*=//' | tr -d ':' | tr 'a-f' 'A-F' \
          | sed 's/\(..\)/\1:/g;s/:$//')
PY=$(python3 - "$PEM" <<'PY'
import base64, hashlib, sys
body, inb = [], False
for line in open(sys.argv[1]):
    if line.startswith("-----BEGIN"): inb = True; continue
    if line.startswith("-----END"): break
    if inb: body.append(line.strip())
d = hashlib.sha256(base64.b64decode("".join(body))).hexdigest().upper()
print(":".join(d[i:i+2] for i in range(0, len(d), 2)))
PY
)
echo
echo "固件 pem_fingerprint()  : $FIRMWARE"
echo "服务端 Python 那套      : $PY"
echo "openssl 官方说法        : $OPENSSL"
echo
rc=0
ok_echo() { printf '\033[32m✓\033[0m %s\n' "$1"; }
[ "$FIRMWARE" = "$OPENSSL" ] || { echo "✗ 固件算出来的指纹与 openssl 不一致 —— 板子屏幕上会显示一个错的指纹，用户永远对不上"; rc=1; }
[ "$PY" = "$OPENSSL" ]       || { echo "✗ 服务端算出来的指纹与 openssl 不一致 —— 管理页显示的值不可信"; rc=1; }
[ "$FIRMWARE" = "$PY" ]      || { echo "✗ 两端算出来的指纹不一致 —— 用户在板子和管理页上会看到两个不同的值"; rc=1; }
[ $rc = 0 ] && echo "✓ 固件、服务端、openssl 三方一致（用户逐段比对的两个值确实同源）"

# ── 5. 边界：带 CRLF 的 PEM、以及证书链（两段）必须与服务端行为一致
python3 - "$PEM" "$OUT" <<'PY'
import sys
pem = open(sys.argv[1]).read()
open(sys.argv[2] + "/crlf.pem", "w").write(pem.replace("\n", "\r\n"))
open(sys.argv[2] + "/chain.pem", "w").write(pem + pem)      # 两段：服务端只取第一段
PY
CRLF=$("$OUT/fp" "$OUT/crlf.pem")
[ "$CRLF" = "$FIRMWARE" ] && echo "✓ CRLF 换行的 PEM 算出来一样（NAS 上文件是 LF，但别处传来的可能是 CRLF）" \
                          || { echo "✗ CRLF 版本算出来不一样：$CRLF"; rc=1; }
CHAIN=$("$OUT/fp" "$OUT/chain.pem")
if [ "$CHAIN" = "$FIRMWARE" ]; then
  echo "✓ 两段证书拼在一起时只认第一段（与服务端一致）"
else
  echo "✗ 两段证书时固件算了别的值（${CHAIN}）—— 服务端只取第一段，两端会对不上"
  rc=1
fi
exit $rc
