#!/bin/bash
# 本机端到端冒烟测试：构建 .fpk → 按真机的目录布局解开 → 把 9 个生命周期脚本
# 按「安装 → 启动 → 改设置 → 升级 → 卸载」走一遍。
#
# 用法：nas/fpk/test_lifecycle.sh [起始端口]
# 依赖：bash + python3 + curl；有 fnpack 就用 fnpack 打包，没有就退化成 tar。
#
# 为什么要真的解开 .fpk 再测：fnOS 把 app.tgz 的内容直接解到 target 目录下
# （$TRIM_APPDEST/server/...），不是 $TRIM_APPDEST/app/server/...。这个布局
# 差异在开发机上极容易写错，所以测试必须复刻真机布局，不能直接对着包目录跑。
#
# 测不到的部分：Linux 包用户身份、/proc 与 /sys 的实际可读性、systemd 环境。
# 那些必须在真 NAS 上用 `python3 fnos_collector.py --probe` 复核。
set -u

PKG="$(cd "$(dirname "$0")/nasscreencompanion" && pwd)"
PORT="${1:-8797}"
PORT2=$((PORT + 1))
WORK="$(mktemp -d)"
STAGE="$WORK/apps/nasscreencompanion"     # 模拟 /var/apps/{appname}
CMD="$STAGE/cmd"
TARGET="$STAGE/target"
PASS=0
# 见文件末尾那段说明：这是"有用例根本没跑"的唯一独立证据
EXPECTED_MIN=142
FAIL=0

ok()    { printf '  \033[32m✓\033[0m %s\n' "$*"; PASS=$((PASS + 1)); }
bad()   { printf '  \033[31m✗\033[0m %s\n' "$*"; FAIL=$((FAIL + 1)); }
step()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
check() { if [ "$1" = "$2" ]; then ok "$3"; else bad "$3（期望 $2，实际 $1）"; fi; }

cleanup() {
  [ -x "$CMD/main" ] && "$CMD/main" stop >/dev/null 2>&1
  rm -rf "$WORK"
}
trap cleanup EXIT

CACERT="$STAGE/var/tls/server.crt"
# 排障 CLI 要直接跑服务端脚本（与 cmd/main 里 resolve_python 的选择一致：优先 python312）
PY="$(command -v python3)"
# 遥测面默认是 HTTPS，自签证书就是唯一的 CA；证书还没生成时退回明文，
# 这样"故意关掉 TLS"的用例也能用同一套辅助函数。
curlv() { if [ -s "$CACERT" ]; then curl --cacert "$CACERT" "$@"; else curl "$@"; fi; }
http_code() { curlv -s -o /dev/null -m 3 -w '%{http_code}' "$1" 2>/dev/null; }
admin()     { curl -s -m 3 --unix-socket "$TARGET/app.sock" -H 'X-Trim-Userid: 1000' -H 'X-Trim-Isadmin: 1' "$@"; }

step "0. 构建 .fpk 并按真机布局解开"
# 打包前清掉 Python 字节码缓存：fnpack 会把包目录里的一切原样打进去，
# __pycache__ 是本地跑测试时留下的构建产物（曾经真的进了 .fpk）。
rm -rf "$PKG"/app/server/__pycache__
if command -v fnpack >/dev/null 2>&1; then
  ( cd "$WORK" && fnpack build --directory "$PKG" >/dev/null 2>&1 )
  FPK="$WORK/$(basename "$PKG").fpk"
  [ -f "$FPK" ] && ok "fnpack build 成功（$(du -h "$FPK" | cut -f1)）" || bad "fnpack build 失败"
else
  FPK=""
  printf '  \033[33m!\033[0m fnpack 不在 PATH，退化为直接读包目录\n'
fi

mkdir -p "$STAGE/var" "$STAGE/etc" "$TARGET"
if [ -n "$FPK" ]; then
  tar xzf "$FPK" -C "$WORK"
  cp -R "$WORK/cmd" "$CMD"
  tar xzf "$WORK/app.tgz" -C "$TARGET"
else
  cp -R "$PKG/cmd" "$CMD"
  cp -R "$PKG/app/." "$TARGET/"
fi
chmod +x "$CMD"/*

# 真机上这些由 fnOS 注入；这里按 framework 文档的语义给出
export TRIM_APPDEST="$TARGET"
export TRIM_PKGVAR="$STAGE/var"
export TRIM_PKGETC="$STAGE/etc"
export TRIM_APPVER=1.0.0
export TRIM_USERNAME=nasscreencompanion
export TRIM_RUN_USERNAME=nasscreencompanion
export wizard_bind=0.0.0.0 wizard_auth=pairing wizard_docker=false wizard_interval=1.0

[ -f "$TARGET/server/nas_companion_server.py" ] && ok "服务在 target/server/（真机布局）" || bad "服务不在 target/server/"
[ -f "$TARGET/ui/www/index.html" ] && ok "管理页在 target/ui/www/（真机布局）" || bad "管理页不在 target/ui/www/"
[ ! -d "$TARGET/app" ] && ok "target 下没有多余的 app/ 层" || bad "target 下多了一层 app/"

step "0b. 包内容基线（构建产物、个人数据、清单）"
if [ -n "$FPK" ]; then
  LIST="$WORK/pack-list.txt"
  tar tzf "$FPK" > "$LIST"
  tar tzf "$WORK/app.tgz" >> "$LIST"
  ! grep -qE '__pycache__|\.py[co]$' "$LIST" && ok "包里没有 Python 字节码缓存" \
    || bad "包里带了 __pycache__/.pyc（打包前要清掉）"
  ! grep -qE '(^|/)\.DS_Store$' "$LIST" && ok "包里没有 .DS_Store" || bad "包里带了 .DS_Store"
  for need in manifest config/privilege config/resource ICON.PNG ICON_256.PNG cmd/main wizard/install; do
    grep -qx "$need" "$LIST" && ok "包内有 $need" || bad "包内缺少 $need"
  done
  # 秘密扫描：包是可公开分发的，不能出现这台机器/NAS 的任何标识。
  # 注意这条只扫"本机实际值"，不是通用规则——换机器上跑要按当地情况补。
  HITS=""
  for pat in '192\.168\.0\.119' 'APP_WIFI_PASS' 'fnos_config' "$(id -un)"; do
    found=$(grep -rIl -- "$pat" "$WORK/cmd" "$TARGET" "$WORK/config" "$WORK/wizard" 2>/dev/null | head -3)
    [ -n "$found" ] && HITS="$HITS ${pat}→$found"
  done
  [ -z "$HITS" ] && ok "秘密扫描：包内没有 NAS 地址/口令/用户名" || bad "包内出现本机标识：$HITS"
fi

step "0c. 向导表单（用户看到的第一屏）"
# 向导的 JSON 语法 fnpack 会拦，语义错了它不管 —— 而那正是用户第一眼看到的东西。
python3 "$(dirname "$PKG")/wizard_check.py" "$PKG" >"$WORK/wizard.log" 2>&1
check "$?" 0 "向导表单通过 schema 与跨文件校验"
grep -q '向导表单通过校验' "$WORK/wizard.log" || bad "向导校验没跑成功：$(tail -1 "$WORK/wizard.log")"

step "0d. manifest 取值与包内结构（决定包能不能被识别、桌面卡片打不打得开）"
# fnpack 只保证"必要字段在、格式是 key = value"，**不检查取值**：platform 写成
# x64、ctl_stop 写成 yes、desktop_applaunchname 与 ui/config 的入口 ID 对不上，
# 它都放行，而后果在真机上才出现。这里按官方文档核对取值，并交叉核对包内结构。
python3 "$(dirname "$PKG")/manifest_check.py" "$PKG" >"$WORK/manifest.log" 2>&1
check "$?" 0 "manifest 取值合法且与包内结构一致"
grep -q 'manifest 取值合法' "$WORK/manifest.log" || bad "manifest 校验没跑成功：$(tail -1 "$WORK/manifest.log")"
grep -q '对得上' "$WORK/manifest.log" || bad "没核对 desktop_applaunchname 与 ui/config"

step "1. 语法与公共前言"
for f in "$CMD"/*; do
    case "$f" in *.py) continue ;; esac
    bash -n "$f" || bad "语法错误：$f"; done
ok "cmd/ 下所有脚本语法通过"
[ -f "$CMD/_common.sh" ] && ok "公共前言 _common.sh 已随包发出" || bad "缺少 cmd/_common.sh"

# 本地开发机是 macOS、NAS 是 Linux，两边差一点点就会"在这儿好好的、到那儿就炸"。
# 这条只在本地跑得到，恰恰是最该在本地拦住的。
PKG="$PKG" python3 - <<'PY'
import ast, io, os, sys
bad = 0
for root, dirs, files in os.walk(os.environ["PKG"]):
    dirs[:] = [d for d in dirs if d != "__pycache__"]
    for f in sorted(files):
        if not f.endswith(".py"):
            continue
        p = os.path.join(root, f)
        try:
            ast.parse(io.open(p, encoding="utf-8").read(), filename=p, feature_version=(3, 12))
        except SyntaxError as e:
            print(f"{p}:{e.lineno} 用了 3.12 之后才有的语法：{e.msg}")
            bad = 1
sys.exit(bad)
PY
check "$?" 0 "随包的 Python 兼容 python312（NAS 上的运行时，本地是 3.14）"
# BSD/macOS 专有写法：本地能跑、Linux 上不存在。反向的 GNU 专有写法不用查——
# 本机是 BSD 工具链，GNU 写法在这里就会原形毕露。
# bash 在 UTF-8 locale 下会把 `$VAR` 紧跟的多字节字符当成变量名的一部分，报
# `W_PORT?: unbound variable`。这个坑在本项目里踩过至少三次（cmd/ 的 15 处、
# 构建脚本、报告生成脚本各一次），所以把它做成检查而不是靠记性。
# 扫的范围不只是一套随包脚本：这个坑在 cmd/、构建脚本、报告脚本、certcheck 里
# 各踩过一次，所以把所有 shell 脚本一起扫（tools/ 与 nas/fpk/ 下的）。
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MB=$(REPO_ROOT="$REPO_ROOT" python3 - "$CMD" "$REPO_ROOT/tools" "$(dirname "$PKG")" <<'PY'
import io, os, re, sys
bad = []
pat = re.compile(r"\$([A-Za-z_][A-Za-z0-9_]*)(?=[^\x00-\x7f])")
files = []
d0 = sys.argv[1]
for fn in sorted(os.listdir(d0)):                 # cmd/ 下所有文件（含无扩展名的）
    p = os.path.join(d0, fn)
    if os.path.isfile(p):
        files.append(p)
for d in sys.argv[2:]:                            # 其余目录只看 *.sh
    if not os.path.isdir(d):
        continue
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".sh"):
            files.append(os.path.join(d, fn))
for p in files:
    for i, line in enumerate(io.open(p, encoding="utf-8", errors="replace"), 1):
        if line.lstrip().startswith("#"):         # 注释不执行，不算问题
            continue
        for m in pat.finditer(line):
            rel = os.path.relpath(p, os.environ.get("REPO_ROOT", os.getcwd()))
            bad.append(f"{rel}:{i} ${m.group(1)} 后面紧跟非 ASCII 字符（要写 ${{{m.group(1)}}}）")
print("\n".join(bad))
PY
)
if [ -n "$MB" ]; then
  bad "shell 脚本里有 \$VAR 紧跟多字节字符：$(echo "$MB" | head -3 | tr '\n' ' ')"
else
  ok "所有 shell 脚本里都没有 \$VAR 紧跟多字节字符（UTF-8 locale 下会被吞进变量名）"
fi

if grep -qE "sed -i ''|stat -f|date -r|date -v|readlink -f|shasum|base64 -D|mktemp -t" "$CMD"/*; then
  bad "cmd/ 里有 BSD/macOS 专有写法（NAS 上是 GNU 工具链）：$(grep -lE "sed -i ''|stat -f|date -r|date -v|readlink -f|shasum|base64 -D|mktemp -t" "$CMD"/* | tr '\n' ' ')"
else
  ok "cmd/ 里没有 BSD/macOS 专有写法"
fi

step "2. 服务路径解析"
python3 "$TARGET/server/fnos_collector.py" --sample >/dev/null 2>&1
check "$?" 0 "fnos_collector.py 可执行"
python3 -c "
import sys; sys.path.insert(0, '$TARGET/server')
import nas_companion_server as m
assert m.app_dest() == '$TARGET', m.app_dest()
" 2>/dev/null && ok "服务认得的 target 目录与 fnOS 注入一致" || bad "服务解析出的 target 目录不对"

step "3. 安装（install_init → install_callback）"
wizard_port="$PORT" "$CMD/install_init" >/dev/null 2>&1
check "$?" 0 "install_init 通过"
wizard_port="$PORT" "$CMD/install_callback" >/dev/null 2>&1
check "$?" 0 "install_callback 写出配置"
check "$(stat -f '%Lp' "$TRIM_PKGVAR/config.json" 2>/dev/null || stat -c '%a' "$TRIM_PKGVAR/config.json")" 600 "config.json 权限 0600"

step "3b. 依赖故障：依赖的 Python 不在时，用户能看到原因"
# 真机场景：install_dep_apps=python312 声明的依赖没装好（或装在了别处）。
# 要求不只是"退出码非 0"，而是**原因要写进应用中心给用户看的日志**
# （$TRIM_TEMP_LOGFILE）——否则界面上只有一句笼统的"检查未通过"。
NOPY="$WORK/nopython"
mkdir -p "$NOPY/bin"
for f in /bin/* /usr/bin/*; do
  b=$(basename "$f")
  case "$b" in python|python3|python3.*) continue;; esac
  ln -sf "$f" "$NOPY/bin/$b" 2>/dev/null
done
USRLOG="$WORK/install-err.log"
: > "$USRLOG"
env -i PATH="$NOPY/bin" HOME="$WORK" \
  TRIM_APPDEST="$TARGET" TRIM_PKGVAR="$STAGE/var" TRIM_PKGETC="$STAGE/etc" \
  TRIM_APPNAME=nasscreencompanion TRIM_TEMP_LOGFILE="$USRLOG" \
  bash "$CMD/install_init" >/dev/null 2>&1
check "$?" 1 "找不到 Python 时安装前检查拒绝继续"
grep -q "python312" "$USRLOG" && ok "失败原因写进了用户可见日志（不只是 stderr）" \
  || bad "用户可见日志里没有说明原因：$(head -c 120 "$USRLOG")"

step "4. 启动与就绪"
wizard_port="$PORT" "$CMD/main" start >/dev/null 2>&1
check "$?" 0 "main start"
"$CMD/main" status >/dev/null 2>&1
check "$?" 0 "main status = 运行中（0）"
check "$(http_code "https://127.0.0.1:$PORT/api/v1/health")" 200 "遥测面 /api/v1/health"
[ -S "$TARGET/app.sock" ] && ok "Socket 建在 target 目录下（网关要求）" || bad "Socket 不在 target 目录下"

step "4b. 传输加密（默认 HTTPS，指纹可核对，明文不静默通过）"
[ -s "$CACERT" ] && ok "自签证书已生成：$CACERT" || bad "证书没有生成"
[ -s "$STAGE/var/tls/server.key" ] && ok "私钥已生成" || bad "私钥没有生成"
check "$(stat -f '%Lp' "$STAGE/var/tls/server.key" 2>/dev/null || stat -c '%a' "$STAGE/var/tls/server.key")" 600 "私钥权限 0600"
# 指纹必须和 openssl 算出来的一致——板子固定的是这个值，算错了等于信错证书
FP_API=$(admin "http://x/app/nasscreencompanion/api/state" \
  | python3 -c 'import sys,json;print(json.load(sys.stdin)["tls"]["fingerprint"])' 2>/dev/null)
FP_SSL=$(openssl x509 -in "$CACERT" -noout -fingerprint -sha256 2>/dev/null | sed 's/.*=//')
[ -n "$FP_API" ] && check "$FP_API" "$FP_SSL" "管理页显示的指纹 = openssl 算出的 SHA-256 指纹" \
  || bad "管理页读不到 TLS 指纹"
# 明文打到 TLS 端口：只放行取证书这一件事，别的都必须失败。
# 板子首次连接时还不知道该信任谁，ESP-IDF 的 esp-tls 在没有任何 CA 来源时又是
# 直接拒绝连接，所以遥测端口按首字节额外收明文——但明文只能读 identity。
# 这几条是"不能顺着这个口子绕过 TLS 和令牌"的实测依据。
check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/identity")" 200 \
  "明文 GET /api/v1/identity 放行（板子首次取证书的通道）"
for p in /api/v1/status /api/v1/health / /api/state; do
  check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' "http://127.0.0.1:$PORT$p")" 403 \
    "明文 GET $p 被拒（明文不能读遥测）"
done
check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' -X POST -d '{"code":"000000"}' \
  "http://127.0.0.1:$PORT/api/v1/pair")" 403 "明文 POST /api/v1/pair 被拒（配对必须走 TLS）"
check "$(curl -s -m 3 "http://127.0.0.1:$PORT/api/v1/identity" \
  | python3 -c 'import sys,json;d=json.load(sys.stdin);print("uid" in d or "auth_mode" in d)' 2>/dev/null)" \
  "False" "明文 identity 不给 uid/auth_mode/已配对设备数"
check "$(curlv -s -m 3 "https://127.0.0.1:$PORT/api/v1/identity" \
  | python3 -c 'import sys,json;d=json.load(sys.stdin);print("uid" in d and "auth_mode" in d)' 2>/dev/null)" \
  "True" "TLS 上的 identity 仍给完整信息"
check "$(curlv -s -o /dev/null -m 3 -w '%{http_code}' "https://127.0.0.1:$PORT/api/v1/identity")" 200 \
  "HTTPS 校验证书后可读 /api/v1/identity"
# identity 在鉴权之前，板子靠它拿到指纹做首次信任
check "$(curlv -s -m 3 "https://127.0.0.1:$PORT/api/v1/identity" \
  | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d["tls"], d["tls_fingerprint"]==sys.argv[1])' "$FP_SSL" 2>/dev/null)" \
  "True True" "identity 未鉴权即可给出指纹（板子首次信任用）"
# 板子固定证书要的是 PEM 本身：它必须自己算指纹，不能信 JSON 里那个字符串
# （否则中间人同时伪造证书和指纹字段就能骗过肉眼比对）。
check "$(curlv -s -m 3 "https://127.0.0.1:$PORT/api/v1/identity" \
  | python3 -c '
import sys, json, base64, hashlib
d = json.load(sys.stdin)
pem = d.get("cert_pem") or ""
b64 = "".join(l for l in pem.splitlines() if l and not l.startswith("-----"))
der = base64.b64decode(b64)
fp = ":".join("%02X" % b for b in hashlib.sha256(der).digest())
print(fp == sys.argv[1])
' "$FP_SSL" 2>/dev/null)" \
  "True" "identity 给出的 cert_pem 自算指纹 = 服务端指纹（板子固定的依据）"

step "4c. 依赖故障：机器上没有 openssl 时不崩、不静默降级"
# P3 验收里的"依赖故障测试"。NAS 上正常情况下有 openssl（自签证书靠它生成），
# 但"依赖不在"必须是个被设计过的状态而不是崩溃或悄悄退回明文却不说。
# 这里在**另一个端口**上起一个进程内实例，不碰上面正在跑的那个服务。
python3 - "$TARGET/server" "$PORT2" <<'PY' 2>/dev/null
import http.client, importlib.util, os, socket, sys, tempfile, threading, time
srv_dir, _ = sys.argv[1], sys.argv[2]
tmp = tempfile.mkdtemp()
os.environ.update(TRIM_APPDEST=tmp, TRIM_PKGVAR=tmp, TRIM_PKGETC=tmp, TRIM_APPNAME="nasscreencompanion")
spec = importlib.util.spec_from_file_location("nsc", os.path.join(srv_dir, "nas_companion_server.py"))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

real = m._openssl_bin
m._openssl_bin = lambda: None            # 假装这台机器上没有 openssl
try:
    info = m.prepare_tls()               # 关键：不能抛异常
    assert info["configured"] is True, info
    assert info["enabled"] is False, info
    assert info.get("error"), "失败原因必须写下来，否则用户只看到'没加密'不知道为什么"
    cfg = dict(m.DEFAULT_CONFIG); cfg.update(port=0, bind="127.0.0.1")
    st = m.State(cfg)
    servers, _ = m.make_servers(st, cfg, bind_socket=False)
    tcp = [s for s in servers if not getattr(s, "is_unix", False)]
    assert tcp, "遥测面没起来：TLS 失败不该把整个服务拖下水"
    tcp = tcp[0]
    port = tcp.server_address[1]
    threading.Thread(target=tcp.serve_forever, daemon=True).start(); time.sleep(0.3)
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    c.request("GET", "/api/v1/health"); r = c.getresponse(); r.read(); c.close()
    assert r.status == 200, r.status
    assert st.tls.get("enabled") is False and st.tls.get("error"), st.tls
    tcp.shutdown(); tcp.server_close()
finally:
    m._openssl_bin = real
PY
check "$?" 0 "没有 openssl 时：prepare_tls 不抛异常、报告启用失败、遥测面照常起来（明文）"

step "5. 启动幂等"
wizard_port="$PORT" "$CMD/main" start >/dev/null 2>&1
check "$?" 0 "重复 start 不出错"

step "5b. 被 SIGKILL 之后还能干净重启（残留 Socket 不挡路）"
# 真机上常见：OOM killer、掉电、应用中心里强杀。进程没了，但 app.sock 这个
# **文件**会留在 target 目录里。下一次 bind 同一个路径就是 EADDRINUSE，
# 而 .sock 是文件不是端口——用户看到的是"管理页打不开"，完全看不出问题在哪。
# 两层防线都要能挡住：cmd/main start 里的 cleanup_socket（只删真 socket），
# 以及服务端 UnixHTTPServer.server_bind() 里的 unlink。这里从**用户路径**
# （kill -9 之后照常点启动）验一遍。
KILL_PID=$(cat "$TRIM_PKGVAR/app.pid" 2>/dev/null)
kill -9 "$KILL_PID" 2>/dev/null
sleep 1
kill -0 "$KILL_PID" 2>/dev/null && bad "SIGKILL 之后进程还在？" || ok "进程已被强杀"
[ -S "$TARGET/app.sock" ] && ok "残留的 Socket 文件确实还在（这就是要测的前提）" \
                         || bad "前提不成立：Socket 文件没留下，这条测不到东西"
wizard_port="$PORT" "$CMD/main" start >/dev/null 2>&1
check "$?" 0 "强杀之后照常启动（残留 Socket 没有挡住）"
check "$(admin -o /dev/null -w '%{http_code}' "http://x/app/nasscreencompanion/api/state")" 200 "管理面重新可用"
check "$(curlv -s -o /dev/null -m 3 -w '%{http_code}' "https://127.0.0.1:$PORT/api/v1/health")" 200 "遥测面也回来了"

step "6. 管理面（走 Unix Socket + 网关身份头）"
check "$(admin -o /dev/null -w '%{http_code}' "http://x/app/nasscreencompanion/")" 200 "管理页可打开"
admin "http://x/app/nasscreencompanion/" | grep -q '<meta charset' && ok "返回的是真管理页，不是兜底页" || bad "管理页内容是兜底页"
check "$(admin -o /dev/null -w '%{http_code}' "http://x/app/nasscreencompanion/api/state")" 200 "/api/state 可读"
check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' --unix-socket "$TARGET/app.sock" "http://x/app/nasscreencompanion/api/state")" 403 "无网关身份头时被拒"
check "$(admin -o /dev/null -w '%{http_code}' "http://x/app/nasscreencompanion/api/probe")" 200 "/api/probe 可读（P0 权限矩阵）"

step "6b. 管理页 JS 真跑一遍（真实响应 + 每个按钮都点一次）"
# 前面所有断言都是用 curl 打接口，页面的 JavaScript 一次都没被执行过（只有
# node --check 过语法）。语法检查发现不了拼错的属性名——而 JS 读一个不存在的
# 属性只会得到 undefined，不会报错，页面会把错的东西安静地画出来。
if command -v node >/dev/null 2>&1; then
  RESP="$WORK/page_responses.json"
  python3 - "$TARGET/app.sock" "$RESP" <<'PY' >/dev/null 2>&1
import json, socket, sys, http.client
sock, out_path = sys.argv[1], sys.argv[2]
class C(http.client.HTTPConnection):
    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(sock)
def call(method, path, body=None):
    c = C('x')
    c.request(method, '/app/nasscreencompanion' + path, body=body,
              headers={'X-Trim-Userid': '1000', 'X-Trim-Isadmin': '1',
                       'Content-Type': 'application/json'})
    r = c.getresponse(); d = r.read(); c.close()
    try:
        return json.loads(d)
    except ValueError:
        return {'raw': d.decode('utf-8', 'replace')[:200]}
out = {}
for p in ('/api/state', '/api/status-now', '/api/probe', '/api/diagnostics', '/api/logs'):
    out['GET ' + p] = call('GET', p)
out['POST /api/pairing/new'] = call('POST', '/api/pairing/new', '{}')
out['POST /api/pairing/cancel'] = call('POST', '/api/pairing/cancel', '{}')
out['POST /api/collector/docker'] = call('POST', '/api/collector/docker', '{"enabled":false}')
out['POST /api/devices/revoke'] = call('POST', '/api/devices/revoke', '{"name":"__none__"}')
cfg = out['GET /api/state']['config']
out['POST /api/config'] = call('POST', '/api/config', json.dumps({
    'port': cfg['port'], 'bind': cfg['bind'], 'interval': cfg['interval'],
    'hist_len': cfg['hist_len'], 'auth_mode': cfg['auth_mode'],
    'tls_enabled': cfg.get('tls_enabled', True)}))
json.dump(out, open(out_path, 'w'), ensure_ascii=False)
PY
  node "$(dirname "$PKG")/page_check.js" "$TARGET/ui/www/index.html" "$RESP" \
       >"$WORK/pagecheck.log" 2>&1
  check "$?" 0 "管理页脚本全部路径跑通（$(tail -1 "$WORK/pagecheck.log" | tr -d '\r')）"
  grep -q '导航按钮切换正常' "$WORK/pagecheck.log" || bad "导航按钮没跑全"
else
  printf '  \033[33m!\033[0m 没有 node，跳过管理页 JS 体检\n'
fi

step "6c. 管理页 ↔ 服务端：路径与请求体字段名"
# 页面发出去的字段名服务端不认识时**不会报错**：_apply_config() 是逐键判断的，
# 多一个没人认的键它一声不吭。用户看到的是"点了保存、提示已保存、设置没变"。
# page_check.js 抓不到这个（它喂的是预制响应，不看请求体）。
python3 "$(dirname "$PKG")/api_check.py" "$PKG" >"$WORK/api.log" 2>&1
check "$?" 0 "页面调的端点都存在、请求体字段名服务端都认"
grep -q '每个端点都存在' "$WORK/api.log" || bad "接口核对没跑成功：$(tail -1 "$WORK/api.log")"

step "6d. 每个管理接口都要真的干活（不是返回 200 就算数）"
# 管理页 JS 那个阶段（6b）会把这些接口真调一遍，但**只把响应记下来喂给页面**，
# 不断言结果——接口返回个错误它也照收。这一步把它们的**效果**钉住。
check "$(admin -o /dev/null -w '%{http_code}' "http://x/app/nasscreencompanion/healthz")" 200 "/healthz 别名可用"

ST_NOW=$(admin "http://x/app/nasscreencompanion/api/status-now")
echo "$ST_NOW" | python3 -c 'import json,sys; d=json.load(sys.stdin); assert "cpu" in d and "mem" in d' 2>/dev/null \
  && ok "/api/status-now 返回的是状态快照而不是空壳" || bad "/api/status-now 的内容不对"

check "$(admin -o /dev/null -w '%{http_code}' -X POST -d '{}' "http://x/app/nasscreencompanion/api/pairing/cancel")" 200 "/api/pairing/cancel 可用"

# 容器开关：点了必须**真的生效**（不然用户看到开关跳了、数据没变）
admin -X POST -d '{"enabled":true}' "http://x/app/nasscreencompanion/api/collector/docker" >/dev/null
python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["config"]["docker_enabled"] is True' \
  <<<"$(admin "http://x/app/nasscreencompanion/api/state")" 2>/dev/null \
  && ok "打开容器采集后 /api/state 里确实变成了开" || bad "容器开关点了没生效"
admin -X POST -d '{"enabled":false}' "http://x/app/nasscreencompanion/api/collector/docker" >/dev/null
python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["config"]["docker_enabled"] is False' \
  <<<"$(admin "http://x/app/nasscreencompanion/api/state")" 2>/dev/null \
  && ok "再关回去也生效" || bad "关不回去"

# 每个管理 POST 都必须先过网关身份校验。漏一个就是"没登录也能改配置"。
NOAUTH_FAIL=0
for ep in api/config api/pairing/new api/pairing/cancel api/devices/revoke api/collector/docker api/shutdown; do
  code=$(curl -s -o /dev/null -m 3 -w '%{http_code}' --unix-socket "$TARGET/app.sock" \
         -X POST -H 'Content-Type: application/json' -d '{}' "http://x/app/nasscreencompanion/$ep")
  [ "$code" = "403" ] || { NOAUTH_FAIL=$((NOAUTH_FAIL + 1)); warn "  $ep 没有身份头时返回 ${code}（应为 403）"; }
done
check "$NOAUTH_FAIL" 0 "六个管理 POST 在缺少网关身份头时全部 403"
# 非管理员也必须被挡（网关会带 X-Trim-Isadmin: 0）
code=$(curl -s -o /dev/null -m 3 -w '%{http_code}' --unix-socket "$TARGET/app.sock" \
       -H 'X-Trim-Userid: 1000' -H 'X-Trim-Isadmin: 0' -X POST -d '{}' \
       "http://x/app/nasscreencompanion/api/config")
check "$code" 403 "非管理员改配置被拒（403）"

step "7. 配对与令牌"
CODE=$(admin -X POST -d '{}' "http://x/app/nasscreencompanion/api/pairing/new" \
        | python3 -c 'import sys,json;print(json.load(sys.stdin)["code"])' 2>/dev/null)
[ -n "$CODE" ] && ok "取得配对码 $CODE" || bad "取不到配对码"
TOK=$(curlv -s -m 3 -X POST -d "{\"code\":\"$CODE\",\"name\":\"p4-7b\"}" \
       "https://127.0.0.1:$PORT/api/v1/pair" \
       | python3 -c 'import sys,json;print(json.load(sys.stdin).get("token",""))' 2>/dev/null)
[ -n "$TOK" ] && ok "配对换到令牌" || bad "配对失败"
check "$(curlv -s -o /dev/null -m 3 -w '%{http_code}' "https://127.0.0.1:$PORT/api/v1/status")" 401 "不带令牌读状态被拒（401）"
check "$(curlv -s -o /dev/null -m 3 -w '%{http_code}' -H "Authorization: Bearer $TOK" "https://127.0.0.1:$PORT/api/v1/status")" 200 "带令牌读状态成功"

devcount() { python3 -c 'import json,sys;print(len(json.load(open(sys.argv[1]))["devices"]))' \
                     "$TRIM_PKGVAR/config.json" 2>/dev/null || echo 0; }
[ "$(devcount)" = "1" ] && ok "配对后设备表里正好 1 台" || bad "配对后设备数不对（$(devcount)）"

step "7b. 协议契约（旧板子必须继续能读）"
# 计划里承诺过：/api/v1/status、history、health 的既有字段名和类型不能变，
# 新增信息只能是加字段。这里把这个承诺写成断言，改协议前先看这一段。
CONTRACT=$(python3 - "$PORT" "$TOK" "$CACERT" <<'PY'
import json, ssl, sys, urllib.request

port, tok, cacert = sys.argv[1], sys.argv[2], sys.argv[3]
base = "https://127.0.0.1:%s" % port
ctx = ssl.create_default_context(cafile=cacert)


def get(path):
    req = urllib.request.Request(base + path, headers={"Authorization": "Bearer " + tok})
    with urllib.request.urlopen(req, timeout=5, context=ctx) as r:
        return json.load(r)


bad = []
st = get("/api/v1/status")

# v=1 是老固件用来判断协议版本的字段，必须原样留着
if st.get("v") != 1:
    bad.append("status.v 不是 1（实际 %r）" % st.get("v"))
def typed(value, t):
    if isinstance(value, bool) and t is not bool:
        return False
    return isinstance(value, t)


for k, t in (("host", str), ("ts", int), ("ready", bool), ("uptime_s", (int, float))):
    if not typed(st.get(k), t):
        bad.append("status.%s 类型不对（%r）" % (k, st.get(k)))
for seg in ("cpu", "mem", "net"):
    if not isinstance(st.get(seg), dict):
        bad.append("status.%s 不是对象" % seg)
for seg in ("vols", "raid", "disks", "temps", "docker", "alerts"):
    if not isinstance(st.get(seg), list):
        bad.append("status.%s 不是数组" % seg)

# 伴侣层的新增字段：加字段不算破坏契约，但既然声明了就必须在
for k in ("proto", "seq", "modules", "caps", "trunc"):
    if k not in st:
        bad.append("status 缺少新增字段 %s" % k)
if st.get("proto") != 2:
    bad.append("status.proto 不是 2（实际 %r）" % st.get("proto"))
if not isinstance(st.get("modules"), dict):
    bad.append("status.modules 不是对象")

h = get("/api/v1/history")
if h.get("cols") != ["ts", "cpu", "mem", "rx_kbs", "tx_kbs"]:
    bad.append("history.cols 变了：%r" % (h.get("cols"),))
rows = h.get("rows")
if not isinstance(rows, list):
    bad.append("history.rows 不是数组")
else:
    for row in rows:
        if not isinstance(row, list) or len(row) != 5:
            bad.append("history 行不是 5 列：%r" % (row,))
            break
        if not all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in row):
            bad.append("history 行里有非数字：%r" % (row,))
            break

hl = get("/api/v1/health")
for k in ("ok", "ts", "age_s", "host"):
    if k not in hl:
        bad.append("health 缺少字段 %s" % k)

print("; ".join(bad))
PY
)
[ -z "$CONTRACT" ] && ok "status / history / health 契约未变（v=1 保留，新增字段齐全）" \
  || bad "契约破坏：$CONTRACT"
python3 -c 'import json,sys;d=json.load(open(sys.argv[1]));assert len(d["devices"])==1' \
  "$TRIM_PKGVAR/config.json" 2>/dev/null && ok "设备已落盘" || bad "设备未落盘"
grep -q "$TOK" "$TRIM_PKGVAR/config.json" 2>/dev/null && bad "明文令牌被写入配置" || ok "配置里不存明文令牌"

step "7c. 板子读的字段，NAS 必须都在（跨语言字段名核对）"
# 板子用 cJSON 按字段名取值，取不到就是 0/空，不报错也不打日志。NAS 侧改个名字，
# 板子只会安静地显示错的东西——这条链路上唯一的防线就是"名字两边是同一份清单"。
# 脚本从固件源码现抓它读了哪些字段（不手抄），再逐项到 NAS 的产出里核对。
python3 "$(dirname "$PKG")/contract_check.py" >"$WORK/contract.log" 2>&1
check "$?" 0 "字段名两边对得上（$(tail -1 "$WORK/contract.log")）"

step "7d. 板子模拟器：把板子的配对动作序列原样走一遍"
# 固件那半边在主机上跑不了，"板子上配对到底成不成"在真机之前是空白。但板子做的事
# 就是五步标准 HTTP/TLS，可以在这里复现——也包括两条否定断言：固定一张别的证书
# 必须握手失败（且配一条不校验的对照连接，证明失败来自校验而不是连不通），
# 以及明文只放行 identity。
python3 "$(dirname "$PKG")/board_sim.py" --host 127.0.0.1 --port "$PORT" \
        --socket "$TARGET/app.sock" >"$WORK/boardsim.log" 2>&1
check "$?" 0 "板子模拟器全过（$(tail -1 "$WORK/boardsim.log" | tr -d '\r')）"
grep -q '固定一张别的证书时握手失败' "$WORK/boardsim.log" || bad "没有跑到证书固定的否定断言"
# 基准在**所有配对动作都做完之后**取：后面几处"令牌一台不少"的断言都对着它比，
# 这样再往套件里加配对用例也不会把写死的数字弄红。
NDEV=$(devcount)
[ "$NDEV" = "2" ] && ok "模拟器又配了一台，设备表 ${NDEV} 台（基准）" || bad "设备表数量不对（${NDEV}）"

step "7g. history 的 ?n= ：板子只有 32 KB 缓冲，不能让 NAS 随便发"
# NAS 的 hist_len 可以配到 3600 行（约 151 KB），而板子的接收缓冲只有 HIST_BUF_SIZE
# （32 KB）。超出的部分会被截断、JSON 解不出来，于是板子每秒重试一次、曲线回填
# 永远不成功，而且**用户完全看不出**。所以板子请求时带 ?n=，NAS 只回最近的 n 行。
hfield() {   # hfield <查询串> <rows|last_ts>
  curlv -s -m 5 -H "Authorization: Bearer $TOK" "https://127.0.0.1:$PORT/api/v1/history$1" \
    | python3 -c '
import json, sys
d = json.load(sys.stdin)
rows = d.get("rows") or []
print(len(rows) if sys.argv[1] == "rows" else (rows[-1][0] if rows else "空"))
' "$2" 2>/dev/null || echo "取不到"
}
# **必须先攒够行数**：只有 3 行的时候，"?n=10 回了 3 行"既可能是正确夹取、也可能是
# NAS 根本没理会 n —— 两种实现看起来一模一样。第一次证伪就是这么被放过去的。
admin -X POST -d '{"interval":0.2}' "http://x/app/nasscreencompanion/api/config" >/dev/null
sleep 6
admin -X POST -d '{"interval":1.0}' "http://x/app/nasscreencompanion/api/config" >/dev/null
near() {   # near <实际> <期望> <容差>
  local d=$(( $1 - $2 )); [ "$d" -lt 0 ] && d=$(( -d ))
  [ "$d" -le "$3" ] && echo OK || echo "差 $d 行"
}
ALL=$(hfield "" rows)
# 先确认基线**真的有数据**：没有的话下面几条会拿两个失败值互比，全变成空断言
# （第一版就是这样：服务其实在另一个端口上，五条里三条"通过"了）
case "$ALL" in ''|*[!0-9]*) bad "history 拿不到数据（${ALL}），下面几条会变成空断言"; ALL=0 ;;
               *) [ "$ALL" -ge 10 ] && ok "基线：攒到 $ALL 行（够让 ?n= 显出差别的下限是 10）" \
                                     || bad "只攒到 $ALL 行，?n= 的断言分不出「没理会 n」和「夹取」" ;;
esac

check "$(hfield "?n=10" rows)" 10 "?n=10 只回 10 行（现在总共有 $ALL 行）"
check "$(near "$(hfield "?n=99999" rows)" "$ALL" 3)" "OK" "?n 超过实际行数时被夹到实际行数（±3 行，采集端还在追加）"
# 采集端每 1 s 追加一行，两次调用之间行数会涨——所以这两条比"接近"，不比"相等"
check "$(near "$(hfield "?n=0" rows)" "$ALL" 3)" "OK" "?n=0 行为不变，仍是全部（±3 行，采集端还在追加）"
check "$(near "$(hfield "?n=abc" rows)" "$ALL" 3)" "OK" "?n 是垃圾值时退回全部，不报错"
# 关键性质：要回来的必须是**最近**的那一段，不是最早的。
# **这条一开始是偶发红的**：它拿两次独立请求的"最后一行 ts"去比，而采集端每 1 s
# 追加一行——两次请求之间正好落进一个新样本时，全量那次就比 ?n=1 那次新一行。
# 连跑 7 次才逮到一次（`期望 …534，实际 …533`）。改成"必须是全量结果最后两行之一"，
# 允许两次请求之间多出一行，就不再随采样节拍抖动。
N1=$(hfield "?n=1" last_ts)
case "$(curlv -s -m 5 -H "Authorization: Bearer $TOK" "https://127.0.0.1:$PORT/api/v1/history" \
        | python3 -c '
import json, sys
rows = (json.load(sys.stdin).get("rows") or [])[-2:]
print(" ".join(str(r[0]) for r in rows))
' 2>/dev/null)" in
  *"$N1"*) ok "?n=1 给的是最近那一行（${N1}，全量结果最后两行之内）" ;;
  *) bad "?n=1 给的是 ${N1}，不在全量结果最后两行里 —— 那它可能给的是最早的那一段" ;;
esac

# 默认完整发送清单；只有显式数量策略才声明 limits。不可恢复旧硬件数量上限。
check "$(curlv -s -m 5 -H "Authorization: Bearer $TOK" "https://127.0.0.1:$PORT/api/v1/status" \
  | python3 -c '
import json, sys
d = json.load(sys.stdin)
lim = (d.get("trunc") or {}).get("limits") or {}
trunc = d.get("trunc") or {}
bad = ["limits"] if lim else []
for k in ("docker", "disks", "alerts", "vols", "raid", "temps", "interfaces"):
    rows = (d.get("net") or {}).get("interfaces") if k == "interfaces" else d.get(k)
    if isinstance(rows, list) and trunc.get("totals", {}).get(k) != len(rows):
        bad.append(k)
    if trunc.get("dropped", {}).get(k, 0):
        bad.append(k + "-dropped")
print("OK" if not bad else "缺或不对：" + ",".join(bad))
' 2>/dev/null)" "OK" "默认清单全量且总数与遗漏统计一致"

step "7e. 两个排障 CLI，以及"重签证书会作废已配对设备"这条性质"
# 这两个 CLI 之前一次都没跑过：
#   --check-port  端口占用检测（cmd/install_init 也用它）
#   --tls-regen   无条件重签证书 —— **指纹会变，已配对设备全部失效**
# 后者是被写进用户手册的性质，所以这里不只验"命令能跑"，而是把它该有的后果
# 验出来，顺便把**生效时机**也钉住：重签只改磁盘上的文件，正在跑的进程手里
# 还是旧证书，要重启才换 —— 第一版断言写成"重签后立刻用新证书能连"，当场红了，
# 因为服务端根本没重新加载。

"$PY" "$TARGET/server/nas_companion_server.py" --check-port "$PORT" >/dev/null 2>&1
check "$?" 1 "--check-port 对已占用的端口返回 1"
"$PY" "$TARGET/server/nas_companion_server.py" --check-port "$((PORT + 20))" >/dev/null 2>&1
check "$?" 0 "--check-port 对空闲端口返回 0"

cp "$CACERT" "$WORK/old.crt"
cp "$STAGE/var/tls/server.key" "$WORK/old.key"
FP_BEFORE=$(openssl x509 -in "$CACERT" -noout -fingerprint -sha256 | sed 's/.*=//')
"$PY" "$TARGET/server/nas_companion_server.py" --tls-regen >/dev/null 2>&1
check "$?" 0 "--tls-regen 执行成功"
FP_AFTER=$(openssl x509 -in "$CACERT" -noout -fingerprint -sha256 | sed 's/.*=//')
[ "$FP_BEFORE" != "$FP_AFTER" ] && ok "重签之后磁盘上的指纹确实变了" \
                              || bad "重签之后指纹没变（--tls-regen 没干活）"
# 生效时机：还没重启，进程手里是旧证书
check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' --cacert "$WORK/old.crt" \
        "https://127.0.0.1:$PORT/api/v1/identity")" 200 \
      "重签后未重启：旧证书仍然连得上（证书是启动时加载的，没有热换）"
# 重启之后换新：旧信任作废、新证书可用
wizard_port="$PORT" "$CMD/main" stop >/dev/null 2>&1
wizard_port="$PORT" "$CMD/main" start >/dev/null 2>&1
check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' --cacert "$WORK/old.crt" \
        "https://127.0.0.1:$PORT/api/v1/identity")" 000 \
      "重启后：按旧证书固定必须握手失败（已配对设备确实被作废）"
check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' --cacert "$CACERT" \
        "https://127.0.0.1:$PORT/api/v1/identity")" 200 \
      "重启后：按新证书固定仍然可用"

# **收尾必须还原**：这一段动的是整个套件共用的证书文件，后面的阶段（比如 8b
# 的"关掉再打开 TLS 指纹不变"）拿 4b 时记下的指纹做比较，不还原就会在别处
# 报红——而且报得让人完全看不懂（第一版就是这样）。
cp "$WORK/old.crt" "$CACERT"
cp "$WORK/old.key" "$STAGE/var/tls/server.key"
wizard_port="$PORT" "$CMD/main" stop >/dev/null 2>&1
wizard_port="$PORT" "$CMD/main" start >/dev/null 2>&1
FP_BACK=$(openssl x509 -in "$CACERT" -noout -fingerprint -sha256 | sed 's/.*=//')
[ "$FP_BACK" = "$FP_BEFORE" ] && ok "收尾已把证书还原（后面的阶段不受影响）" \
                             || bad "证书没还原干净：$FP_BACK"

step "7f. 三条从没验过的性质：限流、内网免令牌、历史长度真的会变"
# 都是"接口存在、参数认、返回 200"但**从没验过后果**的东西。

# ① 配对尝试限流（PAIR_ATTEMPT_LIMIT=5 / 300s）。这是 6 位配对码唯一的防线：
#    没有它，局域网里任何人几秒钟就能穷举完。第 6 次必须 429。
CODES_429=""
for i in 1 2 3 4 5 6; do
  code=$(curlv -s -o /dev/null -m 3 -w '%{http_code}' -X POST -d '{"code":"000000","name":"brute"}' \
         "https://127.0.0.1:$PORT/api/v1/pair")
  [ "$i" = "6" ] && CODES_429=$code
done
check "$CODES_429" 429 "连错 6 次之后被限流（429）—— 6 位码的穷举防线在这儿"

# ② auth_mode=lan：内网免令牌。这是向导里的一个选项，之前从没验过它真的放行。
admin -X POST -d '{"auth_mode":"lan"}' "http://x/app/nasscreencompanion/api/config" >/dev/null
check "$(curlv -s -o /dev/null -m 3 -w '%{http_code}' "https://127.0.0.1:$PORT/api/v1/status")" 200 \
      "切换成内网免令牌后，不带令牌也能读（这就是这个模式的含义）"
admin -X POST -d '{"auth_mode":"pairing"}' "http://x/app/nasscreencompanion/api/config" >/dev/null
check "$(curlv -s -o /dev/null -m 3 -w '%{http_code}' "https://127.0.0.1:$PORT/api/v1/status")" 401 \
      "切回配对令牌后立刻又要求令牌（没有黏在宽松模式上）"

# ③ hist_len 改了，历史真的会变短 —— 不是"返回 200 就算数"。
# **这条第一次写错了**：我断言"改成 40 之后行数 <= 45"，而实例当时才跑了几秒、
# 本来就只有 4 行——上界天然成立，于是把"配置根本没交给采集器"这个 bug 放过去了
# （证伪时才发现）。所以先造出**足够多的样本**，并把这件事本身断言出来：
# 没有足够样本，上界就是个空断言。
rows_now() {
  curlv -s -m 5 -H "Authorization: Bearer $TOK" "https://127.0.0.1:$PORT/api/v1/history" \
    | python3 -c 'import json,sys; print(len(json.load(sys.stdin)["rows"]))' 2>/dev/null || echo -1
}
admin -X POST -d '{"interval":0.2,"hist_len":300}' "http://x/app/nasscreencompanion/api/config" >/dev/null
sleep 10
ROWS_MANY=$(rows_now)
if [ "$ROWS_MANY" -ge 25 ]; then
  ok "先攒够样本：$ROWS_MANY 行（下面收上限才有意义）"
else
  bad "只攒到 $ROWS_MANY 行样本，收上限的断言会变成空断言"
fi
# 收紧上限：20 低于允许范围（30~3600），所以这一步同时验了**非法值必须报错**
# ——以前它返回 200 + {"ok":true,"changed":["hist_len 必须在 30~3600 之间"]}，
# 管理页把这句话当字段名显示成「已保存：hist_len 必须在 30~3600 之间」，而设置没变。
BAD=$(admin -o /dev/null -w '%{http_code}' -X POST -d '{"hist_len":20}' "http://x/app/nasscreencompanion/api/config")
check "$BAD" 400 "超出范围的 hist_len 返回 400（不是"保存成功"）"
# 先把采样间隔拉长，再去收上限：这样"立刻截断"和"等下一次采样再截断"才分得开。
# （不拉长的话，采样循环每 0.2 s 就会自己裁一次，两种实现看起来一模一样——
#   第一次的证伪就是这么被我放过去的。）
admin -X POST -d '{"interval":5.0}' "http://x/app/nasscreencompanion/api/config" >/dev/null
sleep 1
admin -X POST -d '{"hist_len":45}' "http://x/app/nasscreencompanion/api/config" >/dev/null
sleep 1
ROWS_FEW=$(rows_now)
if [ "$ROWS_FEW" -ge 0 ] && [ "$ROWS_FEW" -le 48 ] && [ "$ROWS_FEW" -lt "$ROWS_MANY" ]; then
  ok "hist_len 收到 45 之后掉到 ${ROWS_FEW} 行（原来 ${ROWS_MANY} 行，上限真的生效）"
else
  bad "hist_len 收到 45 之后还有 ${ROWS_FEW} 行（原来 ${ROWS_MANY} 行，上限没生效）"
fi
admin -X POST -d '{"interval":1.0,"hist_len":300}' "http://x/app/nasscreencompanion/api/config" >/dev/null

step "8. 改设置要真的换端口（config_init → config_callback）"
wizard_port="$PORT2" wizard_auth=lan "$CMD/config_init" >/dev/null 2>&1
check "$?" 0 "config_init 通过"
wizard_port="$PORT2" wizard_auth=lan "$CMD/config_callback" >/dev/null 2>&1
check "$?" 0 "config_callback 重启服务"
check "$(http_code "https://127.0.0.1:$PORT2/api/v1/health")" 200 "新端口 $PORT2 已就绪"
check "$(http_code "https://127.0.0.1:$PORT/api/v1/health")" 000 "旧端口 $PORT 已释放"
check "$(devcount)" "$NDEV" "改设置后已配对设备一台不少"

# 向导表单六个字段**每一个**都要真的落到 config.json 里。
# 只验一个端口是不够的：apply_config() 是"传 7 个位置参数"的写法，漏传一个
# 就会用默认值把它悄悄改回去，而界面上看起来一切正常。
wizard_port="$PORT2" wizard_bind=local wizard_auth=pairing wizard_docker=true \
  wizard_tls=true wizard_interval=2.5 "$CMD/config_callback" >/dev/null 2>&1
check "$?" 0 "六个向导字段一起改，config_callback 通过"
check "$(python3 -c '
import json,sys
d = json.load(open(sys.argv[1]))
want = {"port": int(sys.argv[2]), "bind": "127.0.0.1", "auth_mode": "pairing",
        "docker_enabled": True, "tls_enabled": True, "interval": 2.5}
bad = [k for k, v in want.items() if d.get(k) != v]
print("OK" if not bad else "错的是 " + ",".join(bad))
' "$TRIM_PKGVAR/config.json" "$PORT2")" "OK" "六个字段在 config.json 里都对得上"
# 向导管不着的键不许被顺手抹掉（hist_len 是管理页里改的）
check "$(python3 -c '
import json,sys
print(json.load(open(sys.argv[1])).get("hist_len"))
' "$TRIM_PKGVAR/config.json")" "300" "向导管不着的 hist_len 没被覆盖掉"
# 改回原样，免得影响后面的阶段
wizard_port="$PORT2" wizard_bind=lan wizard_auth=lan wizard_docker=false \
  wizard_tls=true wizard_interval=1.0 "$CMD/config_callback" >/dev/null 2>&1

step "8b. 关掉 TLS 后确实是明文，且证书不被删掉"
wizard_port="$PORT2" wizard_auth=lan wizard_tls=false "$CMD/config_callback" >/dev/null 2>&1
check "$?" 0 "config_callback 接受 wizard_tls=false"
check "$(curl -s -o /dev/null -m 3 -w '%{http_code}' "http://127.0.0.1:$PORT2/api/v1/health")" 200 \
  "关闭后明文 HTTP 可用（排障路径通）"
check "$(admin "http://x/app/nasscreencompanion/api/state" \
  | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d["tls"]["configured"], d["tls"]["enabled"])' 2>/dev/null)" \
  "False False" "管理页如实显示 TLS 已关闭"
[ -s "$CACERT" ] && ok "关掉 TLS 不会删掉证书（下次开启指纹不变）" || bad "证书被删了"
# 再打开：指纹必须和之前一模一样，否则已配对的板子会全部失效
wizard_port="$PORT2" wizard_auth=lan wizard_tls=true "$CMD/config_callback" >/dev/null 2>&1
check "$?" 0 "重新打开 TLS"
check "$(admin "http://x/app/nasscreencompanion/api/state" \
  | python3 -c 'import sys,json;print(json.load(sys.stdin)["tls"]["fingerprint"])' 2>/dev/null)" \
  "$FP_SSL" "重新开启后指纹不变（已配对设备不用重新确认）"

step "9. 升级（upgrade_init → upgrade_callback）"
TRIM_OLD_APPVER=0.9.0 wizard_port="$PORT2" "$CMD/upgrade_init" >/dev/null 2>&1
check "$?" 0 "upgrade_init 停服并备份配置"
[ -f "$TRIM_PKGVAR/config.json.pre-0.9.0" ] && ok "旧配置已备份" || bad "没有备份旧配置"
TRIM_OLD_APPVER=0.9.0 wizard_port="$PORT2" "$CMD/upgrade_callback" >/dev/null 2>&1
check "$?" 0 "upgrade_callback"
check "$(http_code "https://127.0.0.1:$PORT2/api/v1/health")" 200 "升级后服务自动恢复运行"
python3 -c 'import json,sys;d=json.load(open(sys.argv[1]));assert len(d["devices"])==int(sys.argv[2])' \
  "$TRIM_PKGVAR/config.json" "$NDEV" 2>/dev/null && ok "升级后设备令牌一台不少" || bad "升级丢失设备令牌"

step "10. 卸载（uninstall_init → uninstall_callback）"
"$CMD/uninstall_init" >/dev/null 2>&1
check "$?" 0 "uninstall_init 停服"
"$CMD/main" status >/dev/null 2>&1
check "$?" 3 "服务已停"
[ -S "$TARGET/app.sock" ] && bad "Socket 残留" || ok "Socket 已清理"
"$CMD/uninstall_callback" >/dev/null 2>&1
check "$?" 0 "uninstall_callback"
check "$(http_code "https://127.0.0.1:$PORT2/api/v1/health")" 000 "端口已释放"

step "11. 端口冲突时不越界"
python3 -c '
import socket, sys, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", int(sys.argv[1]))); s.listen(1); time.sleep(25)
' "$PORT2" &
BLOCKER=$!
sleep 1
wizard_port="$PORT2" "$CMD/install_init" >/dev/null 2>&1
check "$?" 1 "端口被占用时安装前检查拒绝继续"
kill "$BLOCKER" 2>/dev/null
wait "$BLOCKER" 2>/dev/null
kill -0 "$BLOCKER" 2>/dev/null && bad "占用者没被本应用杀掉？" || ok "占用者由测试自己收尾，应用没有动它"
# 只认本次测试这套目录下的实例：开发机上可能同时跑着别的副本（长测、手工起的），
# 用一个泛化的进程名去匹配会把它们算成"残留"。
pgrep -f "$TARGET/server/nas_companion_server.py --serve" >/dev/null 2>&1 \
  && bad "有服务进程残留" || ok "没有残留进程"

step "12. 管理面 Socket 建不起来时，遥测面不受影响"
# 真机上的对应故障：target 目录对包用户不可写。这里用一个同名目录让 bind 失败。
# 卸载已删除配置；重新安装必须明确用本段随后检查的端口。
wizard_port="$PORT2" "$CMD/install_callback" >/dev/null 2>&1
mkdir -p "$TARGET/app.sock/blocker"
START_OUT=$("$CMD/main" start 2>&1)
check "$?" 0 "Socket 失败时启动仍然成功（遥测面是开发板要的）"
echo "$START_OUT" | grep -q "管理面 Socket 没有出现" && ok "启动时给出了 Socket 失败的警告" \
  || bad "Socket 失败被静默吞掉了"
check "$(http_code "https://127.0.0.1:$PORT2/api/v1/health")" 200 "遥测面仍然可用"
"$CMD/main" stop >/dev/null 2>&1
rm -rf "$TARGET/app.sock"
"$CMD/main" status >/dev/null 2>&1
check "$?" 3 "收尾：服务已停"


step "12c. 两条从没走过的恢复路径：日志轮转、config.json 损坏"
# ① 日志轮转（>2MB 时 service.log → service.log.1）。这套东西是给"跑几个月"准备的，
#    平时永远走不到；真坏了的表现是日志无限长大，而没人会发现。
python3 - "$TRIM_PKGVAR/service.log" <<'PY'
import sys
with open(sys.argv[1], "a", encoding="utf-8") as f:
    f.write(("x" * 100 + "\n") * 22000)      # ~2.2MB
PY
BEFORE=$(wc -c < "$TRIM_PKGVAR/service.log" | tr -d ' ')
ok "先把 service.log 灌到 ${BEFORE} 字节（超过 2MB 的轮转线）"
wizard_port="$PORT" "$CMD/main" start >/dev/null 2>&1
check "$?" 0 "日志超限后服务照常启动"
[ -f "$TRIM_PKGVAR/service.log.1" ] && ok "service.log 已轮转成 service.log.1" \
                                    || bad "日志没有轮转（.1 不存在）"
AFTER=$(wc -c < "$TRIM_PKGVAR/service.log" | tr -d ' ')
[ "$AFTER" -lt 262144 ] && ok "轮转后当前日志只有 ${AFTER} 字节（重新开始写）" \
                        || bad "轮转后当前日志仍有 ${AFTER} 字节，没截断"
"$CMD/main" stop >/dev/null 2>&1

# ② config.json 损坏。用户手工编辑、升级中途掉电、磁盘满，都可能留下半个文件。
#    必须明确失败且保留原文件；启动默认服务会在后续 flush 时覆盖配对和设置。
cp "$TRIM_PKGVAR/config.json" "$WORK/config.good" 2>/dev/null
printf '{ 这不是合法 JSON' > "$TRIM_PKGVAR/config.json"
cp "$TRIM_PKGVAR/config.json" "$WORK/config.corrupt"
if curlv -s -o /dev/null -m 2 "https://127.0.0.1:8798/api/v1/health" 2>/dev/null; then
  bad "8798 上已经有东西在跑，这一条测不了（先清掉再跑）"
else
  wizard_port="$PORT" "$CMD/main" start >/dev/null 2>&1
  check "$?" 1 "config.json 损坏时拒绝启动"
  if curlv -s -o /dev/null -m 2 "https://127.0.0.1:8798/api/v1/health" 2>/dev/null; then
    bad "损坏配置不应启动默认服务"
  else
    ok "没有启动默认服务"
  fi
  cmp -s "$WORK/config.corrupt" "$TRIM_PKGVAR/config.json" \
    && ok "损坏配置原始字节保留" || bad "损坏配置被默认值覆盖"
  grep -q '配置读取失败，保留原文件并停止启动' "$TRIM_PKGVAR/service.log" \
    && ok "日志写明启动失败及保留文件" || bad "日志未说明配置失败"
fi
[ -f "$WORK/config.good" ] && cp "$WORK/config.good" "$TRIM_PKGVAR/config.json"
rm -f "$TRIM_PKGVAR/service.log.1"

step "12b. 不理会 SIGTERM 的进程会被升级到 SIGKILL（用户验收问的正是"有没有残留进程"）"
# 用户在验收第 5 步会亲自去看"有没有残留进程"。所以"发了信号"不等于"停掉了"，
# 这条**不测优雅路径**（stage 10 已经测了），专门测"进程赖着不走"时会不会升级、
# 以及**停完到底有没有真的停**。
# **放在最后**：这条要覆盖 pid 文件去停一个 stub 进程，中间态不能留给别的阶段。
# 第一版它插在 5b 之后，结果 stop_process 停的是 stub、真服务还活着，而它顺手
# 把 app.sock 删了 —— 后面 6~12 阶段连锁失败（78 通过 / 59 失败）。
# 注意别写成 STUB=$(... & echo $!)：那个 $! 是**子 shell 的**后台作业号，
# 而子 shell 立刻就退出了，拿到的要么是空的、要么是错的 pid（第一版就是这样，
# 于是三条断言全变成空断言——只有最后那条"必须等够 8 秒"把它揪了出来）。
bash -c 'trap "" TERM; sleep 300' &
STUB=$!
sleep 0.5
kill -0 "$STUB" 2>/dev/null && ok "起了一个忽略 SIGTERM 的进程（pid ${STUB}）" \
                           || bad "造不出忽略 SIGTERM 的进程，这条测不到东西"
echo "$STUB" > "$TRIM_PKGVAR/app.pid"
START=$(date +%s)
wizard_port="$PORT" "$CMD/main" stop >/dev/null 2>&1
RC=$?
ELAPSED=$(( $(date +%s) - START ))
check "$RC" 0 "赖着不走的进程最终被停掉（stop 返回 0）"
kill -0 "$STUB" 2>/dev/null && bad "进程 ${STUB} 还活着 —— 脚本却说了「已停止」" \
                             || ok "进程确实不在了（不是嘴上说说）"
[ "$ELAPSED" -ge 8 ] && ok "确实走完了优雅等待（${ELAPSED}s ≥ 8s）才升级到 SIGKILL" \
                     || bad "只用 ${ELAPSED}s 就返回了，没等够——说明它没走升级路径"
wait "$STUB" 2>/dev/null || true    # 收掉 shell 那句 "Killed: 9" 的作业通知，别混进日志

# **断言条数下限。**
# 第 42 轮我把一个阶段整段挪到了文件第一行（`#!/bin/bash` 之前），脚本照样能跑
# （bash 不看 shebang），那一段就静默地没执行 —— 而汇总数跟着一起少，
# 「131 个 ✓」配「131 项通过」，**看上去完全正常**，是人工数 ✓ 才发现的。
# 光比"汇总数 vs ✓ 个数"挡不住这种（两边一起少），所以这里钉一个独立的下限：
# **加断言时把 EXPECTED_MIN 一起加**，它只在"有用例没跑"时才会不对。
ASSERTIONS=$((PASS + FAIL))
if [ "$ASSERTIONS" -lt "$EXPECTED_MIN" ]; then
  bad "只跑了 $ASSERTIONS 条断言，少于预期的 $EXPECTED_MIN —— 有用例没执行（整段被挪走、被注释掉、或提前 return 都会这样）"
fi

printf '\n\033[1m结果：%d 项通过，%d 项失败\033[0m\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]
