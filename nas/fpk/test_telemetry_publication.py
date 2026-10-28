#!/usr/bin/env python3
"""Production sampler/HTTP regressions using anonymous data and no NAS access."""
import copy
import contextlib
import http.client
import io
import json
import os
import sys
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from benchmark_telemetry import benchmark, fixture, load, publish

SERVER = Path(os.environ.get('FNOS_TEST_SERVER_DIR',
                            Path(__file__).parent / 'nasscreencompanion/app/server'))
m = load(SERVER.resolve())
c = sys.modules['fnos_collector']


class Publication(unittest.TestCase):
    def setUp(self):
        hostname = patch.object(m.socket, 'gethostname', return_value='anonymous-fixture')
        hostname.start()
        self.addCleanup(hostname.stop)
        self.state = m.State(copy.deepcopy(m.DEFAULT_CONFIG))
        self.collector = self.state.collector
        self.handler = object.__new__(m.Handler)
        self.handler.server = SimpleNamespace(state=self.state, is_unix=True)
        self.handler._require_admin = lambda: 'anonymous-fixture'
        self.responses = []
        self.handler._send = self.send

    def send(self, code, body, *args, **kwargs):
        self.assertEqual(code, 200)
        self.responses.append(body if isinstance(body, bytes) else m.json_bytes(body))

    def request(self, path, query=None):
        self.handler._route_get(path, query or {})
        return json.loads(self.responses[-1])

    def test_summary_routes_do_not_copy_inventory(self):
        publish(self.collector, fixture(12))
        with patch.object(m.copy, 'deepcopy', wraps=copy.deepcopy) as copies:
            health = self.request('/api/v1/health')
            state = self.request('/api/state')
        self.assertTrue(health['ready'])
        self.assertEqual(state['collector']['seq'], 1)
        full = [call for call in copies.call_args_list
                if isinstance(call.args[0], dict) and 'vols' in call.args[0]]
        self.assertEqual(len(full), 0, 'summary reads must not traverse hardware inventories')

    def test_status_reuses_full_payload_but_updates_age(self):
        snap = fixture(12)
        publish(self.collector, snap)
        original_dumps = json.dumps
        full_encodes = []
        def encode(obj, *args, **kwargs):
            if isinstance(obj, dict) and 'vols' in obj:
                full_encodes.append(obj)
            return original_dumps(obj, *args, **kwargs)
        with patch.object(m.copy, 'deepcopy', wraps=copy.deepcopy) as copies, \
                patch.object(m.json, 'dumps', side_effect=encode), \
                patch.object(m.time, 'time', return_value=snap['ts'] + 2):
            first = self.request('/api/v1/status')
            self.assertEqual(self.request('/api/status-now'), snap)
            with patch.object(m.time, 'time', return_value=snap['ts'] + 7):
                second = self.request('/api/v1/status')
        self.assertEqual((first['age_s'], second['age_s']), (2, 7))
        self.assertEqual(second['vols'], snap['vols'])
        self.assertEqual(second['app'], m.APPNAME)
        self.assertEqual(second['app_ver'], m.APPVER)
        full = [call for call in copies.call_args_list
                if isinstance(call.args[0], dict) and 'vols' in call.args[0]]
        self.assertEqual(len(full), 0, 'status reads must not deep-copy a published generation')
        self.assertEqual(len(full_encodes), 0, 'a generation must be encoded once, not per reader')

    def test_published_generation_owns_nested_inventory(self):
        snap = fixture(3)
        expected = copy.deepcopy(snap)
        publish(self.collector, snap)
        snap['vols'][0]['mnt'] = '/producer-mutated'
        snap['docker'].clear()
        snap['cpu']['pct'] = 99
        snap['temps'][0]['c'] = 95
        self.assertEqual(self.request('/api/status-now'), expected)

    def test_benchmark_rejects_incomplete_or_changed_sample(self):
        original = m.Handler._route_get
        for key in ('vols', 'docker', 'temps', 'seq', 'cpu'):
            def corrupt(handler, path, query):
                send = handler._send
                def altered(code, body, *args, **kwargs):
                    if path in ('/api/v1/status', '/api/status-now'):
                        body = json.loads(body) if isinstance(body, bytes) else dict(body)
                        body.pop(key, None)
                    return send(code, body, *args, **kwargs)
                handler._send = altered
                try:
                    return original(handler, path, query)
                finally:
                    handler._send = send
            with self.subTest(missing=key), patch.object(m.Handler, '_route_get', corrupt):
                with self.assertRaisesRegex(RuntimeError, 'sample mismatch'):
                    benchmark(m, 2, 1)

    def test_lifecycle_history_guard_rejects_corrupt_extensions(self):
        # Execute the real lifecycle guard, including its HTTP response decoder.
        script = (Path(__file__).parent / 'test_lifecycle.sh').read_text()
        marker = 'CONTRACT=$(python3 - "$PORT" "$TOK" "$CACERT" <<\'PY\'\n'
        guard = compile(script.split(marker, 1)[1].split('\nPY\n)', 1)[0],
                        'lifecycle-contract', 'exec')
        publish(self.collector, fixture(1))
        status = self.request('/api/v1/status')
        status.update(uptime_s=1, raid=[], disks=[], alerts=[], caps={}, trunc={})
        health = self.request('/api/v1/health')
        history = self.request('/api/v1/history')
        def check(candidate):
            responses = {'status': status, 'health': health, 'history': candidate}
            def reply(req, **kwargs):
                return io.BytesIO(m.json_bytes(responses[req.full_url.rsplit('/', 1)[-1]]))
            output = io.StringIO()
            with patch.object(sys, 'argv', ['contract', '1', 'fixture', 'fixture']), \
                    patch('ssl.create_default_context'), \
                    patch('urllib.request.urlopen', side_effect=reply), \
                    contextlib.redirect_stdout(output):
                exec(guard, {})
            return output.getvalue().strip()
        for temperature in (None, 46.5):
            good = copy.deepcopy(history)
            good['rows'][0][5] = temperature
            self.assertEqual(check(good), '')
        corrupt = []
        reordered = copy.deepcopy(history)
        reordered['cols'][1:3] = ['mem', 'cpu']
        corrupt.append(reordered)
        renamed = copy.deepcopy(history)
        renamed['cols'][5] = 'unknown'
        corrupt.append(renamed)
        short = copy.deepcopy(history)
        short['rows'][0].pop()
        corrupt.append(short)
        for column, value in ((1, True), (1, float('nan')), (5, '46'),
                              (5, True), (5, float('inf'))):
            invalid = copy.deepcopy(history)
            invalid['rows'][0][column] = value
            corrupt.append(invalid)
        for invalid in corrupt:
            with self.subTest(history=invalid):
                self.assertTrue(check(invalid), 'lifecycle guard accepted corrupted history')

    def test_initial_failure_and_recovery_keep_history_consistent(self):
        self.assertFalse(self.request('/api/v1/health')['ready'])
        self.assertIsNone(self.request('/api/v1/status')['age_s'])
        publish(self.collector, fixture(1))
        previous_history = self.request('/api/v1/history')
        invalid = fixture(1)
        invalid['vols'][0]['not_json'] = object()
        publish(self.collector, invalid)
        failed = self.request('/api/status-now')
        self.assertFalse(failed['ready'])
        self.assertIn('TypeError', failed['error'])
        self.assertEqual(self.request('/api/v1/history'), previous_history)
        recovered = fixture(2)
        recovered['seq'] = 3
        publish(self.collector, recovered)
        self.assertEqual(self.request('/api/status-now'), recovered)
        self.assertEqual(self.request('/api/v1/history')['n'], 2)

    def test_status_metadata_overwrites_without_duplicate_json_keys(self):
        snap = fixture(1)
        snap.update(age_s=999, app='producer', app_ver='producer')
        snap['vols'][0]['mnt'] = '/匿名/quote"/newline\n'
        publish(self.collector, snap)
        with patch.object(m.time, 'time', return_value=snap['ts'] - 5):
            status = self.request('/api/v1/status')
        keys = []
        json.loads(self.responses[-1], object_pairs_hook=lambda pairs: keys.extend(k for k, v in pairs) or dict(pairs))
        for key in ('age_s', 'app', 'app_ver'):
            self.assertEqual(keys.count(key), 1)
        self.assertEqual(status['age_s'], 0)
        self.assertEqual(status['app'], m.APPNAME)
        self.assertEqual(status['vols'], snap['vols'])
        self.assertEqual(self.request('/api/status-now'), snap)

    def test_stopped_collector_cannot_publish_late_success_or_failure(self):
        for failure in (False, True):
            with self.subTest(failure=failure):
                coll = m.CompanionCollector()
                initial = dict(coll.snapshot)
                entered, release = threading.Event(), threading.Event()
                def sample():
                    entered.set()
                    if not release.wait(2):
                        raise RuntimeError('test release timed out')
                    if failure:
                        raise OSError('anonymous test failure')
                    return fixture(1)
                coll.sample = sample
                coll.start()
                try:
                    self.assertTrue(entered.wait(1))
                    coll._stop.set()
                    release.set()
                    coll.stop(timeout=2)
                    self.assertEqual(coll.snapshot, initial)
                    self.assertEqual(list(coll.history), [])
                finally:
                    release.set()
                    coll.stop(timeout=2)

    def test_sampler_cadence_uses_monotonic_clock(self):
        waits = []
        self.collector.sample = lambda: fixture(1)
        def wait(duration):
            waits.append(duration)
            self.collector._stop.set()
        self.collector._stop.wait = wait
        clocks = SimpleNamespace(time=unittest.mock.Mock(side_effect=[1000, 0]),
                                 monotonic=unittest.mock.Mock(side_effect=[10, 10.25]))
        with patch.object(c, 'time', clocks):
            self.collector._loop()
        self.assertEqual(waits, [.75], 'wall-clock correction must not pause sampling')

    def test_slow_preparation_does_not_hold_reader_lock(self):
        publish(self.collector, fixture(1))
        candidate = fixture(2)
        candidate['seq'] = 2
        entered, release, read_done = (threading.Event() for _ in range(3))
        original = c.PublishedSnapshot
        def prepare(snap):
            entered.set()
            if not release.wait(2):
                raise RuntimeError('test preparation timed out')
            return original(snap)
        self.collector.sample = lambda: candidate
        self.collector._stop.wait = lambda duration: self.collector._stop.set()
        read_result = []
        def read():
            try:
                read_result.append(self.request('/api/v1/status'))
            finally:
                read_done.set()
        with patch.object(c, 'PublishedSnapshot', side_effect=prepare):
            self.collector.start()
            reader = threading.Thread(target=read)
            try:
                self.assertTrue(entered.wait(1))
                reader.start()
                self.assertTrue(read_done.wait(.5), 'reader blocked behind snapshot preparation')
                self.assertEqual(read_result[0]['seq'], 1)
            finally:
                release.set()
                self.collector.stop(timeout=2)
                if reader.ident is not None:
                    reader.join(2)

    def test_history_keeps_recent_rows_null_quality_and_resize(self):
        for seq in range(4):
            snap = fixture(2)
            snap['seq'] = seq
            snap['ts'] += seq
            if seq == 3:
                snap['modules']['temps'] = {'status': 'stale'}
            publish(self.collector, snap)
        recent = self.request('/api/v1/history', {'n': ['2']})
        self.assertEqual(recent['n'], 2)
        self.assertEqual(recent['cols'], ['ts', 'cpu', 'mem', 'rx_kbs', 'tx_kbs', 'temp_max'])
        self.assertEqual([row[0] for row in recent['rows']], [1700000002, 1700000003])
        self.assertEqual([row[-1] for row in recent['rows']], [40, None])
        for n in ('', 'invalid', '0', '-1', '5000'):
            self.assertEqual(self.request('/api/v1/history', {'n': [n]})['n'], 4)
        self.collector.set_hist_len(2)
        self.assertEqual(self.request('/api/v1/history')['rows'], recent['rows'])

    def test_history_quality_is_per_sample_and_per_module(self):
        for state in ('stale', 'partial', 'denied', 'missing', 'error', 'disabled'):
            with self.subTest(state=state):
                snap = fixture(2)
                snap['modules'] = {key: {'status': state} for key in ('cpu', 'net', 'temps')}
                publish(self.collector, snap)
                row = self.request('/api/v1/history', {'n': ['1']})['rows'][0]
                self.assertEqual(row[1:], [None, 40.5, None, None, None])
        snap = fixture(2)
        snap['errors'] = ['mem']  # Legacy errors without module metadata.
        snap['cpu']['pct'] = 0
        publish(self.collector, snap)
        row = self.request('/api/v1/history', {'n': ['1']})['rows'][0]
        self.assertEqual(row[1:], [0, None, 100, 50, 40])
        publish(self.collector, fixture(2))
        rows = self.request('/api/v1/history', {'n': ['2']})['rows']
        self.assertIsNone(rows[0][2])
        self.assertEqual(rows[1][1:], [12.5, 40.5, 100, 50, 40])

    def http_server(self):
        for name in ('log', 'save_config'):
            mocked = patch.object(m, name)
            mocked.start()
            self.addCleanup(mocked.stop)
        self.state.cfg['devices'] = [{'name': 'fixture', 'last4': 'only',
                                     'sha256': m._token_hash('fixture-only-token')}]
        server = m.ThreadedHTTPServer(('127.0.0.1', 0), m.Handler)
        server.state = self.state
        server.is_unix = False
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .01})
        thread.start()
        def close():
            server.shutdown()
            thread.join(2)
            server.server_close()
        self.addCleanup(close)
        return server

    def http_request(self, server, path, method='GET', auth=True):
        connection = http.client.HTTPConnection(*server.server_address, timeout=3)
        try:
            headers = {'X-Token': 'fixture-only-token'} if auth else {}
            connection.request(method, path, headers=headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_actual_http_auth_headers_and_head(self):
        snap = fixture(3)
        publish(self.collector, snap)
        server = self.http_server()
        for path in ('/api/v1/status', '/api/v1/history'):
            self.assertEqual(self.http_request(server, path, auth=False)[0], 401)
        with patch.object(m.time, 'time', return_value=snap['ts'] + 2):
            code, headers, body = self.http_request(server, '/api/v1/status')
            self.assertEqual(code, 200)
            self.assertEqual(headers['Content-Length'], str(len(body)))
            self.assertEqual(headers['Cache-Control'], 'no-store')
            self.assertEqual(headers['Content-Type'], 'application/json; charset=utf-8')
            self.assertEqual(json.loads(body)['vols'], snap['vols'])
            code, head_headers, head_body = self.http_request(server, '/api/v1/status', method='HEAD')
            self.assertEqual(code, 200)
            self.assertEqual(head_body, b'')
            self.assertEqual(head_headers['Content-Length'], headers['Content-Length'])
        code, _, body = self.http_request(server, '/api/v1/history?n=1')
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(body)['n'], 1)

    def test_active_connection_retires_with_complete_response(self):
        publish(self.collector, fixture(3))
        server = self.http_server()
        server.connection_timeout_s = .8
        closes = 0
        for method in ('GET', 'HEAD'):
            connection = http.client.HTTPConnection(*server.server_address, timeout=2)
            try:
                started = time.monotonic()
                while True:
                    connection.request(method, '/api/v1/status',
                                       headers={'X-Token': 'fixture-only-token'})
                    response = connection.getresponse()
                    body = response.read()
                    self.assertEqual(response.status, 200)
                    if method == 'GET':
                        self.assertEqual(json.loads(body)['vols'], fixture(3)['vols'])
                    else:
                        self.assertEqual(body, b'')
                    if response.getheader('Connection') == 'close':
                        closes += 1
                        self.assertLess(time.monotonic() - started, server.connection_timeout_s)
                        break
                    time.sleep(.02)
                    self.assertLess(time.monotonic() - started, 1.5,
                                    'active connection never announces retirement')
                # Standard clients reconnect without losing the next request.
                connection.request('GET', '/healthz')
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertTrue(json.loads(response.read())['ready'])
            finally:
                connection.close()
        self.assertEqual(closes, 2)

    def test_actual_http_readers_never_mix_generations(self):
        def generation(seq):
            snap = fixture(24)
            snap['seq'] = seq
            for key in ('vols', 'docker', 'temps'):
                for row in snap[key]:
                    row['generation'] = seq
            return snap
        publish(self.collector, generation(1))
        server = self.http_server()
        start = threading.Barrier(9)
        def writer():
            start.wait(2)
            for seq in range(2, 62):
                publish(self.collector, generation(seq))
                time.sleep(.001)
        def reader():
            start.wait(2)
            seen = set()
            for _ in range(20):
                code, _, body = self.http_request(server, '/api/v1/status')
                self.assertEqual(code, 200)
                snap = json.loads(body)
                seen.add(snap['seq'])
                for key in ('vols', 'docker', 'temps'):
                    self.assertEqual(len(snap[key]), 24)
                    self.assertTrue(all(row['generation'] == snap['seq'] for row in snap[key]))
            return seen
        with ThreadPoolExecutor(max_workers=9) as pool:
            written = pool.submit(writer)
            reads = [pool.submit(reader) for _ in range(8)]
            written.result(timeout=5)
            seen = set().union(*(r.result(timeout=5) for r in reads))
        self.assertGreater(max(seen), 1)
        self.assertEqual(self.request('/api/status-now')['seq'], 61)


if __name__ == '__main__':
    unittest.main()
