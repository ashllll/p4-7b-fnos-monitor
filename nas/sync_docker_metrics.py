#!/usr/bin/env python3
"""Check or update trusted inline copies without runtime imports or installer changes."""
import argparse
from pathlib import Path

BASE = Path(__file__).resolve().parent
TARGETS = ('fnos-agent.py', 'fpk/nasscreencompanion/app/server/docker_api.py',
           'fpk/nasscreencompanion/cmd/docker_launch.py')
START = '# BEGIN GENERATED DOCKER METRICS\n'
END = '# END GENERATED DOCKER METRICS\n'


def sync(base=BASE, write=False):
    source = (base / 'docker_metrics.py.inc').read_text()
    compile(source, 'docker_metrics.py.inc', 'exec')
    expected = START + source + END
    stale = []
    for name in TARGETS:
        path = base / name
        text = path.read_text()
        if text.count(START) != 1 or text.count(END) != 1:
            raise ValueError('missing or duplicate generated markers: ' + name)
        start = text.index(START)
        end = text.index(END, start) + len(END)
        if text[start:end] == expected:
            continue
        stale.append(name)
        if write:
            updated = text[:start] + expected + text[end:]
            compile(updated, name, 'exec')
            path.write_text(updated)
    return stale


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true', help='update generated sections')
    args = parser.parse_args()
    stale = sync(write=args.write)
    if stale:
        print(('updated: ' if args.write else 'stale Docker metric copies: ') + ', '.join(stale))
    else:
        print('Docker metric copies match canonical source')
    raise SystemExit(1 if stale and not args.write else 0)
