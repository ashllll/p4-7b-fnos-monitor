#!/usr/bin/env python3
"""审计 M31 回归：令牌的安装 / 升级 / 失败路径（临时根 + 桩 systemctl + 桩健康检查）。

复核意见要的是"可复现的安全安装路径"：fail-closed 改完之后，随包 unit 里既没有
EnvironmentFile，也没有任何东西创建 /etc/fnos-agent.env，默认安装根本起不来；而
agent 的报错文案与结果文档都声称"安装脚本会生成 0600 的令牌文件"。

这条回归把 nas/install-remote.sh 整套在**临时根**里跑一遍（FNAS_INSTALL_ROOT 前缀根、
SYSTEMCTL 桩、FNAS_AGENT_HEALTH_URL 指向本进程里的桩服务），不碰真 NAS、不碰真
systemd、不碰 8799，逐条验证：

  A 首次安装：生成 24 字节令牌、0600、写进 env 文件、unit 会加载它，输出里**只有指纹**；
  B 升级：令牌原样保留（字节级），权限被改坏也会被改回 0600，代码确实换了新的；
  C rotate-token：只在显式调用时换令牌，同样只打指纹；
  D/E 失败路径：unit 缺 EnvironmentFile / 偷偷 --insecure-no-auth → 非零退出，且
      **不落地任何文件**（不留坏 unit，也不造令牌）；
  F 鉴权自证：不带令牌却拿到 200 → 非零退出（绝不"装完就算成功"）；
  G 源文件缺失 → 非零退出；
  H 卸载默认保留令牌，只有 --purge-token 才删；
  I 随包 unit 与 agent 的静态检查：unit 有 EnvironmentFile、没有 --insecure-no-auth；
      agent 在没有 FNAS_TOKEN 的环境中**拒绝启动**（不自动降级成"人人可读"）；
  J 令牌不出现在任何命令行上（不会被 ps 看见）。

临时根里跑不成 root，所以文件属主（root:root）只在真机上生效：这里断言脚本里确实
有 `install -o root -g root` 的 root 分支，属主的最终确认留给人/真机。
"""
import http.server
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
NAS = REPO / "nas"
INSTALL = NAS / "install-remote.sh"
AGENT = NAS / "fnos-agent.py"
UNIT = NAS / "fnos-agent.service"
TOKEN_RE = re.compile(r"\b[0-9a-f]{48}\b")
FAILED = []


def check(name, cond, detail=""):
    print("%s %s%s" % ("✓" if cond else "✗", name, ("　—— " + detail) if detail else ""))
    if not cond:
        FAILED.append(name)
    return cond


def mode_of(p):
    return os.stat(str(p)).st_mode & 0o777


def read_token(env_path):
    for line in Path(env_path).read_text(encoding="utf-8").splitlines():
        if line.startswith("FNAS_TOKEN="):
            return line.split("=", 1)[1].strip()
    return ""


class HealthStub(http.server.ThreadingHTTPServer):
    """桩服务：按 self.code 回 401（鉴权生效）或 200（鉴权没生效）。"""
    allow_reuse_address = True

    def __init__(self, code):
        super().__init__(("127.0.0.1", 0), _StubHandler)
        self.code = code


class _StubHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        self.send_response(self.server.code)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"{}")

    def log_message(self, *a):
        pass


def start_health(code):
    srv = HealthStub(code)
    threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True).start()
    return srv, "http://127.0.0.1:%d/api/v1/health" % srv.server_address[1]


def make_systemctl(bindir):
    """桩 systemctl：记录调用，is-active 回答 0（活着），status 打一行 Active。"""
    p = Path(bindir) / "systemctl"
    p.write_text(
        "#!/bin/sh\n"
        'echo "$@" >> "$SYSTEMCTL_LOG"\n'
        'case "$1" in\n'
        "  is-active) exit 0 ;;\n"
        '  status) echo "  Active: active (running)"; exit 0 ;;\n'
        "esac\n"
        "exit 0\n", encoding="utf-8")
    p.chmod(0o755)
    return p


def run_install(root, bindir, health_url, action="install", py_src=None, unit_src=None,
                wait_s="0"):
    env = dict(os.environ)
    env.update({
        "FNAS_INSTALL_ROOT": str(root),
        "SYSTEMCTL": str(Path(bindir) / "systemctl"),
        "SYSTEMCTL_LOG": str(Path(bindir) / "calls.log"),
        "FNAS_AGENT_HEALTH_URL": health_url,
        "FNAS_AGENT_WAIT_S": wait_s,
    })
    cmd = ["bash", str(INSTALL), action]
    if action == "install":
        cmd += [str(py_src or AGENT), str(unit_src or UNIT)]
    elif action == "uninstall" and unit_src:      # 复用 unit_src 传 --purge-token
        cmd += [str(unit_src)]
    r = subprocess.run(cmd, env=env, capture_output=True, timeout=120)
    # 不让"脚本自己吐了非法 UTF-8"把测试搞崩：解码成替换字符，由断言判红。
    r.stdout = r.stdout.decode("utf-8", "replace")
    r.stderr = r.stderr.decode("utf-8", "replace")
    return r


def main():
    if not INSTALL.is_file():
        print("✗ 找不到 %s" % INSTALL)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        root = tmp / "root"                       # 模拟 NAS 的 /
        bindir = tmp / "bin"
        bindir.mkdir()
        make_systemctl(bindir)
        srv, health_url = start_health(401)
        env_path = root / "etc/fnos-agent.env"
        unit_dst = root / "etc/systemd/system/fnos-agent.service"
        py_dst = root / "usr/local/bin/fnos-agent.py"

        # ---------- A 首次安装 ----------
        r = run_install(root, bindir, health_url)
        out = r.stdout + r.stderr
        check("A 安装脚本的输出是合法 UTF-8（非法字节多半是 $VAR 吞掉了全角字符）",
              "\ufffd" not in out, "出现替换字符的位置：%d" % out.find("\ufffd"))
        check("A 首次安装成功（rc=0）", r.returncode == 0,
              "rc=%d；输出尾部：%s" % (r.returncode, out.strip().splitlines()[-1] if out.strip() else ""))
        tok = read_token(env_path) if env_path.is_file() else ""
        check("A 生成了 24 字节令牌文件", env_path.is_file() and len(tok) == 48 and
              re.fullmatch(r"[0-9a-f]{48}", tok or "") is not None, "令牌长度 %d" % len(tok))
        check("A 令牌文件权限是 600", env_path.is_file() and mode_of(env_path) == 0o600,
              "实际 %s" % (oct(mode_of(env_path)) if env_path.is_file() else "缺文件"))
        check("A 代码与 unit 落地（755/644）",
              py_dst.is_file() and mode_of(py_dst) == 0o755 and
              unit_dst.is_file() and mode_of(unit_dst) == 0o644,
              "py=%s unit=%s" % (mode_of(py_dst) if py_dst.is_file() else "缺",
                                 mode_of(unit_dst) if unit_dst.is_file() else "缺"))
        unit_text = unit_dst.read_text(encoding="utf-8") if unit_dst.is_file() else ""
        check("A 落地的 unit 会加载 EnvironmentFile",
              "EnvironmentFile=-/etc/fnos-agent.env" in unit_text)
        check("A 落地的 unit 没有关掉鉴权",
              "--insecure-no-auth" not in unit_text)
        check("A 输出里没有令牌本身（只有指纹）",
              tok and tok not in out and TOKEN_RE.search(out) is None,
              "输出里出现的 48 位十六进制串：%s" % (TOKEN_RE.findall(out) or "无"))
        fp = __import__("hashlib").sha256(tok.encode()).hexdigest()[:8] if tok else ""
        check("A 输出的指纹对得上令牌", fp and fp in out, "指纹 %s" % fp)
        calls = (bindir / "calls.log").read_text(encoding="utf-8")
        check("A 服务被 reload/enable/restart/is-active/status 一遍",
              all(k in calls for k in ("daemon-reload", "enable", "restart", "is-active", "status")),
              calls.replace("\n", " | ").strip())
        check("A 鉴权自证看到 401", "401" in out)

        # ---------- B 升级：令牌原样保留，权限被改坏也改回来 ----------
        before = env_path.read_bytes()
        env_path.chmod(0o644)                     # 模拟被人改坏
        py_new = tmp / "fnos-agent-upgraded.py"
        py_new.write_text(AGENT.read_text(encoding="utf-8") + "\n# upgrade-marker\n",
                          encoding="utf-8")
        r2 = run_install(root, bindir, health_url, py_src=py_new)
        out2 = r2.stdout + r2.stderr
        check("B 升级成功（rc=0）", r2.returncode == 0, "rc=%d" % r2.returncode)
        check("B 令牌字节级不变（板子上配好的凭据不会因为升级失效）",
              env_path.read_bytes() == before,
              "升级前 %d 字节 / 升级后 %d 字节" % (len(before), len(env_path.read_bytes())))
        check("B 权限被改坏后回到 600", mode_of(env_path) == 0o600, oct(mode_of(env_path)))
        check("B 代码确实换成了新版本", "upgrade-marker" in py_dst.read_text(encoding="utf-8"))
        check("B 升级输出里没有令牌", tok not in out2 and TOKEN_RE.search(out2) is None)

        # ---------- C rotate-token ----------
        r3 = run_install(root, bindir, health_url, action="rotate-token")
        tok2 = read_token(env_path)
        check("C rotate-token 成功且换掉了令牌", r3.returncode == 0 and tok2 and tok2 != tok,
              "rc=%d，是否变化=%s" % (r3.returncode, tok2 != tok))
        check("C rotate-token 后权限仍是 600", mode_of(env_path) == 0o600, oct(mode_of(env_path)))
        check("C rotate-token 输出里只有指纹，没有令牌",
              tok2 not in (r3.stdout + r3.stderr) and TOKEN_RE.search(r3.stdout + r3.stderr) is None)

        # ---------- D unit 缺 EnvironmentFile ----------
        root_d = tmp / "root-d"
        bad_unit = tmp / "unit-no-envfile.service"
        bad_unit.write_text(UNIT.read_text(encoding="utf-8").replace(
            "EnvironmentFile=-/etc/fnos-agent.env", "# 故意拿掉 EnvironmentFile"), encoding="utf-8")
        rd = run_install(root_d, bindir, health_url, unit_src=bad_unit)
        od = rd.stdout + rd.stderr
        check("D 缺 EnvironmentFile 的 unit → 非零退出", rd.returncode != 0, "rc=%d" % rd.returncode)
        check("D 报错说清了原因", "EnvironmentFile" in od)
        check("D 不落地这个坏 unit", not (root_d / "etc/systemd/system/fnos-agent.service").exists())
        check("D 也不给它造令牌文件", not (root_d / "etc/fnos-agent.env").exists())

        # ---------- E 偷偷关鉴权 ----------
        root_e = tmp / "root-e"
        bad_unit2 = tmp / "unit-insecure.service"
        bad_unit2.write_text(UNIT.read_text(encoding="utf-8").replace(
            "--interval 1.0", "--interval 1.0 --insecure-no-auth"), encoding="utf-8")
        re_ = run_install(root_e, bindir, health_url, unit_src=bad_unit2)
        oe = re_.stdout + re_.stderr
        check("E 想带 --insecure-no-auth 的 unit → 非零退出", re_.returncode != 0,
              "rc=%d" % re_.returncode)
        check("E 报错点名了 --insecure-no-auth", "--insecure-no-auth" in oe)
        check("E 同样不落地任何文件",
              not (root_e / "etc/systemd/system/fnos-agent.service").exists() and
              not (root_e / "etc/fnos-agent.env").exists())

        # ---------- F 不带令牌却拿到 200 ----------
        srv.shutdown()
        srv.server_close()
        srv2, health_200 = start_health(200)
        root_f = tmp / "root-f"
        rf = run_install(root_f, bindir, health_200)
        of = rf.stdout + rf.stderr
        check("F 鉴权没生效（200）→ 非零退出", rf.returncode != 0, "rc=%d" % rf.returncode)
        check("F 报错说清了 200 这一支（没带令牌却拿到 200）",
              "没带令牌却拿到 200" in of and "鉴权没生效" in of,
              of.strip().splitlines()[-1] if of.strip() else "")
        srv2.shutdown()
        srv2.server_close()

        # ---------- G 源文件缺失 ----------
        rg = run_install(tmp / "root-g", bindir, "http://127.0.0.1:1/x",
                         py_src=tmp / "nope.py")
        check("G 源文件缺失 → 非零退出且不落地文件",
              rg.returncode != 0 and not (tmp / "root-g/etc/fnos-agent.env").exists(),
              "rc=%d" % rg.returncode)

        # ---------- H 卸载：默认留令牌，--purge-token 才删 ----------
        srv3, health_401 = start_health(401)
        root_h = tmp / "root-h"
        run_install(root_h, bindir, health_401)
        rh = run_install(root_h, bindir, health_401, action="uninstall")
        check("H 卸载默认保留令牌文件", rh.returncode == 0 and
              (root_h / "etc/fnos-agent.env").is_file(),
              "rc=%d" % rh.returncode)
        check("H 卸载删掉了代码与 unit",
              not (root_h / "usr/local/bin/fnos-agent.py").exists() and
              not (root_h / "etc/systemd/system/fnos-agent.service").exists())
        rh2 = run_install(root_h, bindir, health_401, action="uninstall", unit_src="--purge-token")
        check("H --purge-token 才删令牌文件",
              rh2.returncode == 0 and not (root_h / "etc/fnos-agent.env").exists())
        srv3.shutdown()
        srv3.server_close()

    # ---------- I 随包 unit / agent 的静态与行为检查 ----------
    unit_src_text = UNIT.read_text(encoding="utf-8")
    check("I 随包 unit 有 EnvironmentFile", "EnvironmentFile=-/etc/fnos-agent.env" in unit_src_text)
    check("I 随包 unit 没有 --insecure-no-auth", "--insecure-no-auth" not in unit_src_text)
    env = dict(os.environ)
    env.pop("FNAS_TOKEN", None)
    ra = subprocess.run([sys.executable, str(AGENT), "--bind", "127.0.0.1", "--port", "0"],
                        env=env, capture_output=True, text=True, timeout=30)
    check("I 没有令牌时 agent 拒绝启动（不自动降级）", ra.returncode != 0,
          "rc=%d" % ra.returncode)
    check("I 拒绝启动的文案指向 /etc/fnos-agent.env 与 install-remote.sh",
          "/etc/fnos-agent.env" in (ra.stdout + ra.stderr) and
          "install-remote.sh" in (ra.stdout + ra.stderr))
    print("! I 真实端口监听未验证：本用例传 --port 0，仅验证进程拒绝启动")

    # ---------- J 令牌不进命令行 ----------
    lines = INSTALL.read_text(encoding="utf-8").splitlines()
    bad_lines = [ln.strip() for ln in lines
                 if "$tok" in ln and not any(k in ln for k in ("tok=", "printf", "unset tok"))]
    check("J 脚本里没有把令牌塞进命令行（ps 看不到）", not bad_lines,
          "可疑行：%s" % (bad_lines or "无"))
    check("J 脚本里有 root 属主安装分支（真机上才是 root）",
          "-o root -g root" in INSTALL.read_text(encoding="utf-8"))

    if FAILED:
        print("✗ %d 项没通过：%s" % (len(FAILED), "、".join(FAILED)))
        return 1
    print("PASS: 首次安装造令牌（0600）、升级原样保留、失败路径不落地文件、"
          "不带令牌拿不到 200、输出里从不出现令牌本身")
    return 0


if __name__ == "__main__":
    sys.exit(main())
