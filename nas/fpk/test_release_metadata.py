#!/usr/bin/env python3
"""Package/README identity checks use only synthetic archives in a temp directory."""
import io
import hashlib
from pathlib import Path
import tarfile
import tempfile
import unittest
from release_metadata import BEGIN, END, synchronize


class Metadata(unittest.TestCase):
    def test_write_check_tamper_and_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pkg = root/'nas/fpk/nasscreencompanion'
            pkg.mkdir(parents=True)
            (pkg/'manifest').write_text('version = 1.2.10\n')
            with tarfile.open(pkg.with_suffix('.fpk'), 'w:gz') as archive:
                payload = b'anonymous app archive fixture'
                info = tarfile.TarInfo('app.tgz');info.size = len(payload)
                archive.addfile(info, io.BytesIO(payload))
                data = ('version = 1.2.10\nchecksum = '+hashlib.md5(payload).hexdigest()+'\n').encode()
                member = tarfile.TarInfo('manifest');member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
            readme = root/'README.md'
            readme.write_text('before\n'+BEGIN+'\nstale\n'+END+'\nafter\n')
            with self.assertRaises(ValueError): synchronize(root)
            meta = synchronize(root, write=True)
            self.assertEqual(meta['version'], '1.2.10')
            self.assertEqual(meta, synchronize(root))
            self.assertTrue(readme.read_text().startswith('before\n'))
            self.assertTrue(readme.read_text().endswith('\nafter\n'))
            readme.write_text(readme.read_text().replace(meta['sha256'], 'bad-hash'))
            with self.assertRaises(ValueError): synchronize(root)
            synchronize(root, write=True)
            (pkg/'manifest').write_text('version = 1.2.11\n')
            with self.assertRaises(ValueError): synchronize(root, write=True)
            (pkg/'manifest').write_text('version = 1.2.10\n')
            readme.write_text('keep without markers')
            with self.assertRaises(ValueError): synchronize(root, write=True)
            self.assertEqual(readme.read_text(), 'keep without markers')
            with tarfile.open(pkg.with_suffix('.fpk'), 'w:gz') as archive:
                data = b'version = 1.2.10\nchecksum = 00000000000000000000000000000000\n'
                for name, content in [('manifest', data), ('app.tgz', b'changed')]:
                    member = tarfile.TarInfo(name);member.size = len(content)
                    archive.addfile(member, io.BytesIO(content))
            with self.assertRaisesRegex(ValueError, 'checksum differs'):
                synchronize(root, write=True)

    def test_missing_package_never_overwrite_readme(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'README.md').write_text('keep')
            with self.assertRaises(OSError): synchronize(root, write=True)
            self.assertEqual((root/'README.md').read_text(), 'keep')


if __name__ == '__main__': unittest.main()
