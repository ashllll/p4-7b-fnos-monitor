#!/usr/bin/env python3
"""manifest 校验：按官方文档核对取值，并交叉核对包内真实结构。

`fnpack build` 只保证"必要字段在、格式是 key = value"，**不检查取值合不合法**。
但 manifest 决定的是"这个包能不能被识别、能不能装、桌面卡片点不点得开"：
`platform` 写成 `x64`、`ctl_stop` 写成 `yes`、`desktop_applaunchname` 与
`ui/config` 里的入口 ID 对不上——这些 fnpack 都不会拦，而后果在真机上才出现。

规则来自官方文档（developer.fnnas.com/docs/core-concepts/manifest）：
  appname / version / display_name / desc / source / platform /
  maintainer / maintainer_url / distributor / distributor_url /
  os_min_version / os_max_version / ctl_stop / install_type / install_dep_apps /
  desktop_uidir / desktop_applaunchname / service_port / checkport /
  disable_authorization_path / changelog

用法：
    python3 nas/fpk/manifest_check.py [包目录]
    python3 nas/fpk/manifest_check.py --strict      # 把"待填写"占位也算错误
退出码非 0 表示有错误。
"""

import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PKG = os.path.join(HERE, "nasscreencompanion")

BOOLS = {"true", "false"}
KNOWN = {
    "appname", "version", "display_name", "desc", "source", "platform",
    "maintainer", "maintainer_url", "distributor", "distributor_url",
    "os_min_version", "os_max_version", "ctl_stop", "install_type",
    "install_dep_apps", "desktop_uidir", "desktop_applaunchname",
    "service_port", "checkport", "disable_authorization_path", "changelog",
}
REQUIRED = {"appname", "version", "display_name", "desc", "source", "platform",
            "maintainer", "maintainer_url", "desktop_uidir"}
# "待填写"这类占位：本地装机测试无所谓，提交上架前必须填掉
PLACEHOLDER = re.compile(r"待填写|TODO|FIXME|your-|example\.com|xxx", re.I)


def parse(path):
    """manifest 是 `key = value`，`=` 两侧允许空格；value 里可以有 `=`。"""
    out = {}
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            s = line.rstrip("\n")
            if not s.strip() or s.lstrip().startswith("#"):
                continue
            if "=" not in s:
                out.setdefault("__bad__", []).append((lineno, s))
                continue
            k, v = s.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def wiring_problems():
    """四处**分散在不同文件里、必须一致**的常量。

    网关前缀、Socket 文件名、服务端口、应用标识——写错任何一处的后果都是"装上也
    打不开"或者"端口检查看的是别的端口"，而它们之间**没有任何东西互相对照**：

      · manifest 的 appname / service_port
      · app/ui/config 的入口 ID、gatewayPrefix、gatewaySocket
      · 服务端的 APPNAME / GATEWAY_PREFIX / SOCKET_NAME / DEFAULT_CONFIG["port"]
      · wizard/install 里 wizard_port 的默认值

    对照关系：网关前缀必须是 `/app/<appname>`（服务端就是拿 APPNAME 拼的），
    Socket 名必须两边一样（网关按名字去 target 目录里找），端口三处必须一样
    （否则安装时检查的是一个端口、实际监听的是另一个）。
    """
    out = []
    pkg = os.path.join(HERE, "nasscreencompanion")
    mpath = os.path.join(pkg, "manifest")
    cpath = os.path.join(pkg, "app", "ui", "config")
    spath = os.path.join(pkg, "app", "server", "nas_companion_server.py")
    wpath = os.path.join(pkg, "wizard", "install")
    for f in (mpath, cpath, spath, wpath):
        if not os.path.exists(f):
            return [f"缺少 {os.path.relpath(f, HERE)}，没法核对四处常量是否一致"]

    m = dict(re.findall(r"^(\w+)\s*=\s*(.*)$", io.open(mpath, encoding="utf-8").read(), re.M))
    cfg = json.loads(io.open(cpath, encoding="utf-8").read())
    entries = list((cfg.get(".url") or {}).items())
    if len(entries) != 1:
        return [f"app/ui/config 里应当只有一个入口，实际 {len(entries)} 个"]
    eid, ent = entries[0]
    srv = io.open(spath, encoding="utf-8").read()

    def const(name):
        mm = re.search(r'^%s\s*=\s*"([^"]+)"' % name, srv, re.M)
        return mm.group(1) if mm else None

    appname = (m.get("appname") or "").strip()
    srv_appname = const("APPNAME")
    if srv_appname != appname:
        out.append(f"manifest 的 appname={appname}，服务端的 APPNAME={srv_appname}——"
                   f"服务端是用 APPNAME 拼网关前缀的，对不上页面就 404")

    want_prefix = "/app/" + appname
    if ent.get("gatewayPrefix") != want_prefix:
        out.append(f"gatewayPrefix 写的是 {ent.get('gatewayPrefix')}，应当是 {want_prefix}"
                   f"（网关按它转发，写错管理页打不开）")
    if ent.get("url") != ent.get("gatewayPrefix"):
        out.append("桌面入口 url 必须与 gatewayPrefix 一致；缺失时会在应用窗口内打开飞牛桌面")
    if 'GATEWAY_PREFIX = "/app/" + APPNAME' not in srv:
        out.append("服务端不再用 APPNAME 拼 GATEWAY_PREFIX 了——"
                   "那 manifest 改名时它会悄悄跟不上")

    srv_sock = const("SOCKET_NAME")
    if ent.get("gatewaySocket") != srv_sock:
        out.append(f"gatewaySocket 写的是 {ent.get('gatewaySocket')}，而服务端建的是 "
                   f"{srv_sock}—— 网关按名字去 target 目录找，对不上就转不进来")

    if eid != (m.get("desktop_applaunchname") or "").strip():
        out.append(f"入口 ID {eid} 与 manifest 的 desktop_applaunchname 不一致")
    if not eid.startswith(appname):
        out.append(f"入口 ID {eid} 没有以 appname 开头（官方建议入口 ID 用 appname 前缀）")

    # 入口标题必须是**字面量**。踩过：写了 "{display_name}" 以为飞牛会替换，结果桌面图标
    # 上原样显示 {display_name}（用户截图报障）。官方只替换 icon 里的 {0}（图标尺寸）
    # 与 url/port 里的 ${wizard_port}/${wizard_path}，没有 {display_name} 这种变量。
    title = ent.get("title")
    if not isinstance(title, str) or not title.strip():
        out.append("入口 title 是空的——桌面图标会没有名字")
    elif re.search(r"\{[^}]*\}|\$\{[^}]*\}", title):
        out.append(f"入口 title={title!r} 里有未替换的占位符；"
                   f"飞牛不会替换它（只替换 icon 的 {{0}}），桌面图标会原样显示这串字符")
    icon = ent.get("icon") or ""
    if "{0}" in icon and not all(
            os.path.exists(os.path.join(pkg, "app", "ui", "images", f"icon_{n}.png"))
            for n in (64, 256)):
        out.append("icon 用了 {0} 模板，但 app/ui/images/icon_64.png 或 icon_256.png 缺失")

    ports = []
    mp = (m.get("service_port") or "").strip()
    if mp:
        ports.append(("manifest service_port", mp))
    sm = re.search(r'"port":\s*(\d+)', srv)
    if sm:
        ports.append(("服务端默认端口", sm.group(1)))
    wm = re.search(r'"field":\s*"wizard_port".*?"initValue":\s*"?(\d+)"?', io.open(wpath, encoding="utf-8").read(), re.S)
    if wm:
        ports.append(("向导默认端口", wm.group(1)))
    vals = {v for _, v in ports}
    if len(vals) > 1:
        out.append("端口三处对不上：" + "、".join(f"{k}={v}" for k, v in ports)
                   + " —— 安装时检查的是一个端口、实际监听的是另一个")
    if not out:
        print(f"四处常量对得上：appname={appname} 前缀={want_prefix} "
              f"socket={srv_sock} 端口={sorted(vals)[0] if vals else '?'}")
    return out


def layout_evidence_problems():
    """`TRIM_APPDEST` 是整个方案压着的那个假设，核一下官方原文还在不在。

    它错则全错、而且本机测不出来（测试套件复刻的也是同一个假设）。官方文档快照
    在 /tmp 下会被清掉，所以这里**只在快照存在时**核，不存在就跳过——不假装自己
    验过。原文（env.txt）：「TRIM_APPDEST：已安装的 target 目录。」；
    gateway.txt：「飞牛 fnOS 将请求转发到 /var/apps/myapp/target/app.sock」。
    """
    docs = os.path.join("/tmp", "fnos-docs")
    env_txt = os.path.join(docs, "env.txt")
    gw_txt = os.path.join(docs, "gateway.txt")
    if not (os.path.exists(env_txt) and os.path.exists(gw_txt)):
        print("（跳过布局依据核对：/tmp/fnos-docs 快照不在，没法核原文）")
        return []
    out = []
    env = io.open(env_txt, encoding="utf-8", errors="replace").read()
    if "TRIM_APPDEST：已安装的 target 目录" not in env:
        out.append("官方环境变量文档里找不到「TRIM_APPDEST：已安装的 target 目录」这句"
                   "—— 布局假设的原文依据变了，去看 docs/fnos-companion-app-plan.md 那一节")
    gw = io.open(gw_txt, encoding="utf-8", errors="replace").read()
    if "/var/apps/myapp/target/app.sock" not in gw:
        out.append("官方网关文档里找不到 /var/apps/myapp/target/app.sock"
                   "—— Socket 位置这句话变了")
    if not out:
        print("布局依据：官方原文仍然支持「TRIM_APPDEST = target 目录、Socket 在其下」")
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    strict = "--strict" in sys.argv
    pkg = args[0] if args else DEFAULT_PKG

    mpath = os.path.join(pkg, "manifest")
    if not os.path.isfile(mpath):
        print(f"✗ 找不到 {mpath}")
        return 1
    m = parse(mpath)
    problems, warns = [], []

    for lineno, line in m.pop("__bad__", []):
        problems.append(f"第 {lineno} 行不是 key = value：{line!r}")

    # ── 必要字段与未知字段
    for k in sorted(REQUIRED - set(m)):
        problems.append(f"缺必要字段 {k}")
    for k in sorted(set(m) - KNOWN):
        warns.append(f"不认识的字段 {k}（官方文档里没有，可能是拼写错误）")

    # ── 取值合法性
    if m.get("source") != "thirdparty":
        problems.append(f"source={m.get('source')!r}，第三方应用必须是 thirdparty")
    if m.get("platform") not in ("x86", "arm", "all"):
        problems.append(f"platform={m.get('platform')!r} 不在 x86/arm/all 里")
    if "version" in m and not re.fullmatch(r"\d+(\.\d+)*(-[\w.]+)?", m["version"]):
        problems.append(f"version={m['version']!r} 不像「1.0.0」或「2.1.3-beta」")
    for k in ("ctl_stop", "checkport", "disable_authorization_path"):
        if k in m and m[k] not in BOOLS:
            problems.append(f"{k}={m[k]!r} 必须是 true 或 false")
    if m.get("install_type", "") not in ("", "root"):
        problems.append(f"install_type={m['install_type']!r} 只允许留空或 root")
    if "service_port" in m:
        try:
            port = int(m["service_port"])
            if not (1 <= port <= 65535):
                raise ValueError
        except ValueError:
            problems.append(f"service_port={m['service_port']!r} 不是 1~65535 的整数")
    if "install_dep_apps" in m:
        for dep in m["install_dep_apps"].split(":"):
            if not re.fullmatch(r"[\w.+-]+(>[\w.+-]+)?", dep):
                problems.append(f"install_dep_apps 里的 {dep!r} 不符合 name 或 name>版本")
    for k in ("maintainer_url", "distributor_url"):
        if k in m and m[k] and not re.match(r"^https?://", m[k]):
            warns.append(f"{k}={m[k]!r} 不像一个网址")
    for k in ("os_min_version", "os_max_version"):
        if k in m and m[k] and not re.fullmatch(r"\d+(\.\d+)+", m[k]):
            warns.append(f"{k}={m[k]!r} 不像「1.2.0」这样的版本号")

    # ── 与包内真实结构交叉核对
    uidir = m.get("desktop_uidir", "ui")
    uipath = os.path.join(pkg, uidir)
    if not os.path.isdir(uipath if os.path.isdir(uipath) else os.path.join(pkg, "app", uidir)):
        problems.append(f"desktop_uidir={uidir!r}，但包里没有这个目录")
    launch = m.get("desktop_applaunchname")
    if launch:
        cfg = None
        for cand in (os.path.join(pkg, uidir, "config"),
                     os.path.join(pkg, "app", uidir, "config")):
            if os.path.isfile(cand):
                cfg = cand
                break
        if cfg is None:
            problems.append(f"找不到 {uidir}/config，没法核对 desktop_applaunchname")
        else:
            try:
                data = json.load(open(cfg, encoding="utf-8"))
            except ValueError as e:
                problems.append(f"{uidir}/config 不是合法 JSON：{e}")
            else:
                ids = set()
                for grp in data.values():
                    if isinstance(grp, dict):
                        ids |= set(grp)
                if launch not in ids:
                    problems.append(
                        f"desktop_applaunchname={launch!r} 在 {uidir}/config 的入口 ID 里找不到 "
                        f"{sorted(ids)} —— 桌面卡片会打不开")
                else:
                    print(f"  desktop_applaunchname 与 {uidir}/config 的入口 ID 对得上")

    # ── 占位符
    holes = [f"{k}={v!r}" for k, v in m.items()
             if isinstance(v, str) and PLACEHOLDER.search(v)]
    if holes:
        msg = "还没填的占位值：" + "、".join(holes)
        (problems if strict else warns).append(msg + ("（--strict 下算错误）" if strict else ""))

    problems += layout_evidence_problems()
    problems += wiring_problems()

    print(f"manifest 共 {len(m)} 个字段")
    for w in warns:
        print(f"  \033[33m!\033[0m {w}")
    if problems:
        print("发现问题：")
        for p in problems:
            print("  ✗", p)
        return 1
    print("manifest 取值合法，且与包内结构对得上 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
