#!/usr/bin/env python3
"""Fail when the packaged app tree differs from the source tree."""
import argparse
import io
from pathlib import Path
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GENERATED = {'config/privilege', 'config/resource'}
BYTECODE = ('.pyc', '.pyo')


def _source_files(source):
    files = {}
    for path in sorted(source.rglob('*')):
        if not path.is_file():
            continue
        rel = path.relative_to(source).as_posix()
        if (path.name == '.DS_Store' or path.suffix in BYTECODE
                or '__pycache__' in path.parts):
            continue
        files[rel] = path.read_bytes()
    return files


def _packed_files(package):
    with tarfile.open(package, 'r:gz') as outer:
        member = outer.getmember('app.tgz')
        if not member.isfile():
            raise ValueError('app.tgz must be a regular file')
        with outer.extractfile(member) as stream:
            inner = stream.read()
    with tarfile.open(fileobj=io.BytesIO(inner), mode='r:gz') as app:
        files = {}
        for member in app.getmembers():
            if member.isfile():
                with app.extractfile(member) as stream:
                    files[member.name] = stream.read()
        return files


def verify(package=None, source=None):
    package = Path(package or ROOT/'nas/fpk/nasscreencompanion.fpk')
    source = Path(source or ROOT/'nas/fpk/nasscreencompanion/app')
    expected = _source_files(source)
    packed = _packed_files(package)
    extra = sorted(set(packed) - set(expected) - GENERATED)
    missing = sorted(set(expected) - set(packed))
    changed = sorted(name for name in set(expected) & set(packed)
                     if expected[name] != packed[name])
    if missing or extra or changed:
        details = []
        if missing:
            details.append('missing from package: ' + ', '.join(missing))
        if extra:
            details.append('not in source: ' + ', '.join(extra))
        if changed:
            details.append('content differs: ' + ', '.join(changed))
        raise ValueError('; '.join(details))
    return len(expected)


def _write_package(path, files):
    inner = io.BytesIO()
    with tarfile.open(fileobj=inner, mode='w:gz') as app:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            app.addfile(info, io.BytesIO(data))
    data = inner.getvalue()
    with tarfile.open(path, 'w:gz') as outer:
        info = tarfile.TarInfo('app.tgz')
        info.size = len(data)
        outer.addfile(info, io.BytesIO(data))


def _self_test():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root/'app'
        (source/'server').mkdir(parents=True)
        (source/'server'/'a.py').write_bytes(b'x')
        package = root/'app.fpk'

        _write_package(package, {'server/a.py': b'x', 'config/privilege': b'p'})
        assert verify(package, source) == 1

        _write_package(package, {'server/a.py': b'y'})
        try:
            verify(package, source)
        except ValueError as exc:
            assert 'content differs' in str(exc)
        else:
            raise AssertionError('changed file was accepted')

        _write_package(package, {})
        try:
            verify(package, source)
        except ValueError as exc:
            assert 'missing from package' in str(exc)
        else:
            raise AssertionError('missing file was accepted')

        _write_package(package, {'server/a.py': b'x', 'server/b.py': b'z'})
        try:
            verify(package, source)
        except ValueError as exc:
            assert 'not in source' in str(exc)
        else:
            raise AssertionError('extra file was accepted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('package', nargs='?')
    parser.add_argument('source', nargs='?')
    args = parser.parse_args()
    if args.self_test:
        _self_test()
        print('package source sync self-test OK')
        return
    try:
        count = verify(args.package, args.source)
    except (OSError, ValueError, KeyError, tarfile.TarError) as exc:
        parser.exit(1, f'FPK source sync: {exc}\n')
    print(f'FPK source sync: {count} files match')


if __name__ == '__main__':
    main()
