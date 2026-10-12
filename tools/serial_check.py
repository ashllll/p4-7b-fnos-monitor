#!/usr/bin/env python3
"""Check a completed current-firmware boot capture, not physical UI or NAS acceptance."""
import argparse
from pathlib import Path
import re
import sys

MAX_BYTES = 16 * 1024 * 1024
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
BOOT = re.compile(r'\bmain:\s+boot complete\s*\(ui=([^)]*)\)')
CRASH = re.compile(r'Guru Meditation|abort\(\)|assert(?:ion)? failed|Backtrace|\bpanic\b|Rebooting|Stack protection', re.I)
CRITICAL = re.compile(r'\b(?:main|fnos_ui):.*(?:display start failed|UI refresh unavailable|UI refresh timer allocation failed|timer[^\n]*(?:failed|failure)|界面始终|重试任务也起不来)', re.I)


def check(text):
    text = ANSI.sub('', text)
    if not text.strip() or '\ufffd' in text:
        return False, 'empty or undecodable capture'
    if CRASH.search(text):
        return False, 'crash or reset marker'
    if CRITICAL.search(text):
        return False, 'display, refresh or timer failure marker'
    boots = BOOT.findall(text)
    # Old unclassified boot lines and multiple boots cannot prove this boot's availability.
    if len(boots) != 1 or boots[0] != 'created' or len(re.findall(r'\bmain:\s+boot complete\b', text)) != 1:
        return False, 'require exactly one current boot complete (ui=created)'
    if not re.search(r'\bfnos_ui:\s+ui created \(pages=[1-9][0-9]*\)', text):
        return False, 'UI tree creation receipt missing'
    return True, 'current boot tree/refresh availability; physical screen and NAS remain unverified'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args(argv)
    try:
        with args.log.open('rb') as source:
            raw = source.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValueError('capture exceeds byte budget')
        text = raw.decode('utf-8')
    except (OSError, ValueError):
        print('FAIL: unreadable, oversized or invalid UTF-8 capture', file=sys.stderr)
        return 2
    ok, reason = check(text)
    print(('PASS: ' if ok else 'FAIL: ') + reason)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
