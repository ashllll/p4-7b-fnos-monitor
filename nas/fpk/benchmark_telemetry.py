#!/usr/bin/env python3
"""Anonymous production-route benchmark; no NAS, credentials or hardware access."""
import argparse
import copy
import importlib.util
import json
import os
import statistics
import sys
import threading
import time
from pathlib import Path
from types import SimpleNamespace


def fixture(count):
    return {
        'v': 1, 'proto': 2, 'seq': 1, 'ts': 1700000000, 'ready': True,
        'host': 'benchmark', 'cpu': {'pct': 12.5}, 'mem': {'pct': 40.5},
        'net': {'rx_kbs': 100, 'tx_kbs': 50}, 'modules': {},
        'vols': [{'mnt': '/fixture/volume-%d' % i, 'total_gb': 1000,
                  'used_gb': 400, 'pct': 40} for i in range(count)],
        'docker': [{'id': '%064x' % i, 'n': 'fixture-container-%d' % i,
                    'up': True, 'cpu_pct': 1.2, 'mem_mb': 128,
                    'restarts': 0, 'metrics_ts': 1700000000} for i in range(count)],
        'temps': [{'dev': 'fixture-%d' % i, 'dn': 'Fixture device',
                   'ch': 'Core %d' % i, 'c': 40.0} for i in range(count)],
    }


def load(server_dir):
    sys.path.insert(0, str(server_dir))
    spec = importlib.util.spec_from_file_location('bench_server', server_dir / 'nas_companion_server.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def publish(collector, snap):
    # Drive the production sampler once; its Event.wait seam removes only sleeping.
    collector.sample = lambda: snap
    collector._stop = threading.Event()
    collector._stop.wait = lambda duration: collector._stop.set()
    collector._loop()


def benchmark(module, count, repeats):
    cfg = copy.deepcopy(module.DEFAULT_CONFIG)
    state = module.State(cfg)
    state.collector.hostname = 'benchmark'
    snap = fixture(count)
    expected = json.dumps(snap, sort_keys=True)
    publish(state.collector, snap)
    handler = object.__new__(module.Handler)
    handler.server = SimpleNamespace(state=state, is_unix=True)
    handler._require_admin = lambda: 'anonymous-fixture'
    output = [b'']
    def send(code, body, *args, **kwargs):
        if code != 200:
            raise RuntimeError('route did not succeed')
        output[0] = body if isinstance(body, bytes) else module.json_bytes(body)
    handler._send = send
    result = {'entities_per_list': count, 'routes': {}}
    for path in ['/api/v1/health', '/api/state', '/api/v1/status', '/api/status-now']:
        handler._route_get(path, {})
        samples = []
        for _ in range(7):
            start = time.perf_counter_ns()
            for _ in range(repeats):
                handler._route_get(path, {})
            samples.append((time.perf_counter_ns() - start) / repeats / 1000)
        result['routes'][path] = {'median_us': round(statistics.median(samples), 2),
                                  'bytes': len(output[0])}
        body = json.loads(output[0])
        if path in ('/api/v1/status', '/api/status-now'):
            # Outside the timed section; reject lost fields, altered values or
            # changed JSON types even when Python assertions are disabled.
            if not isinstance(body, dict) or any(key not in body for key in snap) or \
                    json.dumps({key: body[key] for key in snap}, sort_keys=True) != expected:
                raise RuntimeError('sample mismatch in ' + path)
    samples = []
    for _ in range(7):
        start = time.perf_counter_ns()
        for _ in range(max(2, repeats // 10)):
            publish(state.collector, snap)
        samples.append((time.perf_counter_ns() - start) / max(2, repeats // 10) / 1000)
    result['publish_median_us'] = round(statistics.median(samples), 2)
    for readers in (1, 4, 32):
        result['publish_plus_%d_status_us' % readers] = round(
            result['publish_median_us'] + readers * result['routes']['/api/v1/status']['median_us'], 2)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server-dir', type=Path,
                        default=Path(__file__).parent / 'nasscreencompanion/app/server')
    parser.add_argument('--counts', type=int, nargs='+', default=[32, 256, 1024])
    parser.add_argument('--repeats', type=int, default=40)
    args = parser.parse_args()
    if args.repeats < 1 or any(count < 0 for count in args.counts):
        parser.error('counts must be nonnegative and repeats positive')
    # Keep metadata and path lengths identical for source and extracted packages.
    os.environ.update(TRIM_APPDEST='/fixture/app', TRIM_PKGVAR='/fixture/var',
                      TRIM_PKGETC='/fixture/etc')
    module = load(args.server_dir.resolve())
    print(json.dumps({'python': sys.version.split()[0], 'scope': 'host production routes; anonymous inventory; no network',
                      'results': [benchmark(module, count, args.repeats) for count in args.counts]}, indent=2))


if __name__ == '__main__':
    main()
