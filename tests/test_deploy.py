"""Exercise deployment publication and failure handling with a local fake Kindle."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class DeployTest(unittest.TestCase):
    def test_publish_and_failed_checksum_preserves_shortcut(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / 'project'
            (project / 'bin').mkdir(parents=True)
            for name in ('deploy', 'InkBoy.sh'):
                shutil.copy(source / 'bin' / name, project / 'bin' / name)
            device = root / 'device'
            (device / 'documents').mkdir(parents=True)
            mocks = root / 'mocks'
            mocks.mkdir()
            shim = '''#!/usr/bin/env python3
import os, pathlib, shutil, subprocess, sys
device = os.environ['FAKE_DEVICE']
if pathlib.Path(sys.argv[0]).name == 'ssh':
    command = sys.argv[-1].replace('/mnt/us', device)
    sys.exit(subprocess.call(['sh', '-c', command]))
src, dst = sys.argv[-2:]
dst = dst.split(':', 1)[1].replace('/mnt/us', device)
if pathlib.Path(src).is_dir():
    shutil.copytree(src, pathlib.Path(dst) / pathlib.Path(src).name)
else:
    shutil.copy(src, dst)
'''
            for name in ('ssh', 'scp'):
                path = mocks / name
                path.write_text(shim)
                path.chmod(0o755)
            env = dict(os.environ, PATH=str(mocks) + ':' + os.environ['PATH'],
                       FAKE_DEVICE=str(device), KINDLE_REMOTE_ROOT='/mnt/us/custom-inkboy')
            shortcut = device / 'documents' / 'InkBoy.sh'
            for run_id, corrupt in [('first', False), ('second', False), ('broken', True)]:
                package = project / 'build' / 'packages' / run_id
                package.mkdir(parents=True)
                (package / 'inkboy').write_text('#!/bin/sh\nexit 0\n')
                (package / 'inkboy').chmod(0o755)
                (package / 'sameboy_libretro.so').write_text('fake core')
                (package / 'MANIFEST').write_text('run_id=' + run_id)
                hashes = subprocess.check_output(['sha256sum', 'inkboy', 'sameboy_libretro.so'], cwd=package)
                (package / 'SHA256SUMS').write_bytes(hashes)
                if corrupt:
                    (package / 'sameboy_libretro.so').write_text('corrupt')
                previous = shortcut.read_bytes() if shortcut.exists() else None
                result = subprocess.run([str(project / 'bin' / 'deploy'), 'install', run_id],
                                        env=env, capture_output=True, text=True)
                if corrupt:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(shortcut.read_bytes(), previous)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('/mnt/us/custom-inkboy/runs/' + run_id, shortcut.read_text())
                    self.assertNotIn('@RUN_DIR@', shortcut.read_text())
                    self.assertTrue(os.access(shortcut, os.X_OK))
            self.assertEqual(list((device / 'documents').glob('.inkboy-*')), [])


if __name__ == '__main__':
    unittest.main()
