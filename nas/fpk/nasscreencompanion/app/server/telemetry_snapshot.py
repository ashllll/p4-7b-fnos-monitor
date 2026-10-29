"""Owned telemetry generation: one encode per sample, short reader lock holds.

The sampler may reuse nested caches. Own them before publication so neither a
later sample nor a reader can alter an in-flight HTTP response. The HTTP layer
receives bytes; only the sampler borrows ``data`` and must treat it as read-only.
"""
import copy
import json


def _encode(value):
    # NaN/Infinity are not valid JSON; fail before publication instead of letting
    # the board receive an unparsable snapshot.
    return json.dumps(value, ensure_ascii=False, separators=(',', ':'),
                      allow_nan=False).encode('utf-8')


class PublishedSnapshot:
    __slots__ = ('data', 'body', '_status_prefix')

    def __init__(self, candidate):
        self.data = copy.deepcopy(candidate)
        self.body = _encode(self.data)
        # These fields belong to each HTTP request, not to the sampler. Preserve
        # the raw snapshot endpoint while overwriting them in /api/v1/status.
        if any(key in self.data for key in ('age_s', 'app', 'app_ver')):
            body = _encode({k: v for k, v in self.data.items()
                            if k not in ('age_s', 'app', 'app_ver')})
        else:
            body = self.body
        # A view avoids retaining a second copy of a potentially large inventory.
        self._status_prefix = memoryview(body)[:-1]

    def summary(self, now):
        ts = self.data.get('ts', 0)
        return {'ready': bool(self.data.get('ready')), 'seq': self.data.get('seq', 0),
                'ts': ts, 'age_s': round(max(0.0, now - ts), 1) if ts else None}

    def status_body(self, app, version, now):
        metadata = _encode({'age_s': self.summary(now)['age_s'],
                            'app': app, 'app_ver': version})
        comma = b',' if len(self._status_prefix) > 1 else b''
        return b''.join((self._status_prefix, comma, memoryview(metadata)[1:]))
