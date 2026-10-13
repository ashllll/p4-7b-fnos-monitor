#!/usr/bin/env python3
"""常驻服务的每请求内存检查：打一批请求，看 RSS 是"平台化"还是"单调上涨"。

为什么单独一个脚本、而不是塞进 test_lifecycle.sh：生命周期测试验的是"装/启/停/
升级/卸载对不对"，这个验的是"一直跑下去会不会涨"，两者的时间尺度差三个数量级。

做法是把服务在**同一个进程里**起来（绑 0 端口、关 TLS、不建管理面 Socket），
混着打开发板会打的那几个端点，按批记当前 RSS。判据是**后段相对中段不再增长**：
- 一次性的预热（导入、分配器 arena、首次触页）会让前几百个请求涨一截，之后必须走平；
- 真的每请求泄漏（比如把请求对象留在某个列表里）会一路涨下去，后段还在涨。

用法：python3 nas/fpk/mem_check.py [请求数，默认 3000]
      --entities 1024 --publish-every 10 --server-dir <解包后的 server>
默认仅测空快照请求；--entities 增加匿名完整清单和持续生产发布，不访问 NAS。
"""

import argparse
import http.client
import json
import os
import sys
from pathlib import Path

# 这个脚本会把 nas_companion_server.py 加载进本进程跑；它顶部 `from fnos_collector
# import …`，于是 Python 会在**源码树里**写下 __pycache__/*.pyc（本机 3.14 的字节码）。
# 之后任何一次直接的 `fnpack build` 都会把它打进 app.tgz —— 60 KB 的无用字节码，
# 而 NAS 上是 python312。禁掉写入，别让检查工具污染要分发的包。
sys.dont_write_bytecode = True
import subprocess
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "nasscreencompanion", "app", "server", "nas_companion_server.py")
PATHS = ["/api/v1/status", "/api/v1/health", "/api/v1/identity", "/api/v1/history"]
BATCH = 500


def rss_kb():
    out = subprocess.run(["ps", "-o", "rss=", "-p", str(os.getpid())],
                         capture_output=True, text=True).stdout.strip()
    return int(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('requests', type=int, nargs='?', default=3000)
    parser.add_argument('--entities', type=int, default=0)
    parser.add_argument('--publish-every', type=int, default=10)
    parser.add_argument('--server-dir', type=Path, default=Path(SERVER).parent)
    args = parser.parse_args()
    if args.entities < 0 or args.publish_every < 1:
        parser.error('entities must be nonnegative and publish-every positive')
    total = args.requests
    if total < BATCH * 6:
        print("样本太少，未判定（请求数至少 3000）")
        return 3
    tmp = tempfile.TemporaryDirectory()
    os.environ.update(TRIM_APPDEST=tmp.name, TRIM_PKGVAR=tmp.name, TRIM_PKGETC=tmp.name,
                      TRIM_APPNAME="nasscreencompanion")
    from benchmark_telemetry import fixture, load, publish
    m = load(args.server_dir.resolve())

    cfg = dict(m.DEFAULT_CONFIG)
    cfg.update(port=0, bind="127.0.0.1", tls_enabled=False, auth_mode="lan")
    state = m.State(cfg)
    generation = 0
    def advance():
        nonlocal generation
        generation += 1
        snap = fixture(args.entities)
        snap.update(seq=generation, ts=1700000000 + generation)
        publish(state.collector, snap)
    if args.entities:
        # Fill the configured bounded history before evaluating steady-state RSS.
        for _ in range(state.collector.hist_len):
            advance()
    servers, _ = m.make_servers(state, cfg, bind_socket=False)
    tcp = [s for s in servers if not getattr(s, "is_unix", False)][0]
    port = tcp.server_address[1]
    thread = threading.Thread(target=tcp.serve_forever, daemon=True)
    thread.start()
    time.sleep(0.3)

    base = rss_kb()
    samples = []
    print(f"起点 RSS={base} KB，活跃线程={threading.active_count()}")
    print(f"匿名实体/清单={args.entities}，每 {args.publish_every} 请求发布一次；无 NAS 访问")
    try:
        for i in range(1, total + 1):
            if args.entities and (i - 1) % args.publish_every == 0:
                advance()
            c = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
            try:
                c.request("GET", PATHS[i % len(PATHS)])
                response = c.getresponse()
                body = response.read()
                if response.status != 200:
                    raise RuntimeError(f"被测端点 {PATHS[i % len(PATHS)]} 返回 {response.status}，未覆盖有效请求")
            finally:
                c.close()
            if args.entities and PATHS[i % len(PATHS)] == '/api/v1/status':
                snap = json.loads(body)
                if snap.get('seq') != generation or any(len(snap.get(key, [])) != args.entities
                                                        for key in ('vols', 'docker', 'temps')):
                    raise RuntimeError('HTTP 未返回当前完整发布代次，拒绝采信内存结果')
            if i % BATCH == 0:
                cur = rss_kb()
                samples.append((i, cur))
                print(f"  {i:6} 个请求  RSS={cur} KB（相对起点 {cur - base:+d}）"
                      f"  活跃线程={threading.active_count()}")
    finally:
        tcp.shutdown()
        tcp.server_close()
        thread.join(timeout=3)
        tmp.cleanup()
    print(f"累计生产发布={generation}，历史行={len(state.collector.read_history())}")

    # 判据：拿后 1/3 的样本比中间 1/3，增长必须很小（噪声量级）。
    if len(samples) < 6:
        print("样本太少，不做判定（请求数至少 3000 才有意义）")
        return 3
    third = len(samples) // 3
    mid = samples[third]
    last = samples[-1]
    grew = last[1] - mid[1]
    span = last[0] - mid[0]
    per_req = grew / span if span else 0.0
    print(f"\n中段 {mid[0]} 个请求时 {mid[1]} KB → 末段 {last[0]} 个请求时 {last[1]} KB"
          f"（{grew:+d} KB / {span} 个请求 = {per_req:+.3f} KB/请求）")
    # 阈值取 0.05 KB/请求：真泄漏通常在 0.5 KB/请求以上，预热残留远低于此。
    if per_req > 0.05:
        print("判定：仍在随请求增长，疑似每请求泄漏 ✗")
        return 1
    print("判定：已平台化，没有每请求泄漏 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
