#!/usr/bin/env python3
"""Derive package identity and the README download block from the actual FPK."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile

BEGIN = '<!-- fpk-metadata:start -->'
END = '<!-- fpk-metadata:end -->'
ROOT = Path(__file__).resolve().parents[2]


def describe(root):
    package = root / 'nas/fpk/nasscreencompanion.fpk'
    with tarfile.open(package, 'r:gz') as archive:
        member = archive.getmember('manifest')
        if not member.isfile() or member.size > 64 * 1024:
            raise ValueError('package manifest must be a small regular file')
        with archive.extractfile(member) as stream:
            manifest = stream.read().decode('utf-8')
        checksums = re.findall(r'^checksum\s*=\s*([0-9a-f]{32})\s*$', manifest, re.M)
        if len(checksums) != 1:
            raise ValueError('package must contain one fnpack checksum')
        app = archive.getmember('app.tgz')
        if not app.isfile():
            raise ValueError('app.tgz must be a regular file')
        with archive.extractfile(app) as stream:
            actual = hashlib.file_digest(stream, 'md5').hexdigest()
        if checksums[0] != actual:
            raise ValueError('fnpack checksum differs from app.tgz')
    match = re.search(r'^version\s*=\s*([0-9]+(?:\.[0-9]+)+)\s*$', manifest, re.M)
    if not match:
        raise ValueError('package version missing or invalid')
    # fnpack appends its own checksum line; all source fields must still match.
    source_manifest = (root/'nas/fpk/nasscreencompanion/manifest').read_text()
    normalized = re.sub(r'^checksum\s*=.*(?:\n|$)', '', manifest, flags=re.M)
    if normalized != source_manifest:
        raise ValueError('package manifest differs from source; rebuild the package')
    data = package.read_bytes()
    return {'file': 'nas/fpk/nasscreencompanion.fpk', 'version': match[1],
            'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def block(meta):
    return (f'{BEGIN}\n仓库当前安装包版本 **{meta["version"]}**，'
            f'大小 **{meta["size"]} 字节**，SHA-256：\n\n'
            f'```text\n{meta["sha256"]}\n```\n{END}')


def synchronize(root, write=False):
    meta = describe(root)
    readme = root/'README.md'
    text = readme.read_text()
    if text.count(BEGIN) != 1 or text.count(END) != 1 or text.index(BEGIN) > text.index(END):
        raise ValueError('README must contain exactly one package metadata block')
    start, end = text.index(BEGIN), text.index(END) + len(END)
    updated = text[:start] + block(meta) + text[end:]
    manifest = root/'nas/fpk/release.json'
    encoded = json.dumps(meta, indent=2, ensure_ascii=False)+'\n'
    if write:
        manifest.write_text(encoded)
        readme.write_text(updated)
    elif not manifest.is_file() or manifest.read_text() != encoded or text != updated:
        raise ValueError('package metadata or README is stale; run release_metadata.py --write')
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    try:
        meta = synchronize(ROOT, args.write)
    except (OSError, ValueError, KeyError, tarfile.TarError) as exc:
        parser.exit(1, f'FPK metadata: {exc}\n')
    print(f'FPK {meta["version"]}: metadata and README match ({meta["size"]} bytes)')


if __name__ == '__main__':
    main()
