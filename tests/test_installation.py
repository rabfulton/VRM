"""A staged system installation launches without access to the source checkout."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


@unittest.skipUnless((ROOT / 'vrm-overlay').is_file() and importlib.util.find_spec('PySide6'),
                     'Build the renderer and install PySide6 for staged installation coverage')
class InstallationTests(unittest.TestCase):
    def test_read_only_install_has_resources_and_writable_user_starters(self):
        with tempfile.TemporaryDirectory(prefix='domovushka-install-') as temporary:
            work = Path(temporary)
            stage = work / 'stage'
            install = subprocess.run([sys.executable, str(ROOT / 'scripts/install.py'), '--prefix', '/usr',
                                      '--destdir', str(stage)], capture_output=True, text=True)
            self.assertEqual(install.returncode, 0, install.stderr)
            purelib = Path(sysconfig.get_path('purelib', vars={'base': '/usr', 'platbase': '/usr'}))
            module = stage / purelib.relative_to('/')
            data = stage / 'usr/share/domovushka'
            renderer = stage / 'usr/bin/vrm-overlay'
            env = dict(os.environ, PYTHONPATH=str(module), VRM_DATA_DIR=str(data), VRM_RENDERER=str(renderer),
                       HOME=str(work), XDG_CONFIG_HOME=str(work / 'config'), XDG_DATA_HOME=str(work / 'data'),
                       XDG_CACHE_HOME=str(work / 'cache'), QT_QPA_PLATFORM='offscreen',
                       QT_QPA_PLATFORMTHEME='basic', QT_QUICK_BACKEND='software')
            help_result = subprocess.run([str(stage / 'usr/bin/domovushka-mcp'), '--help'], cwd=work,
                                         env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(help_result.returncode, 0, help_result.stderr)
            self.assertIn('--companion-socket', help_result.stdout)
            probe = 'import vrm_overlay; print(vrm_overlay.__file__)'
            result = subprocess.run([sys.executable, '-c', probe], cwd=work, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(str(module), result.stdout)
            # Remove write permission even though this staging tree belongs to us.
            for path in data.rglob('*'):
                path.chmod(0o555 if path.is_dir() else 0o444)
            try:
                result = subprocess.run([str(stage / 'usr/bin/domovushka'), '--check'], cwd=work,
                                        env=env, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                user_clip = work / 'data/domovushka/starter/animations/humanoid/idle.cfg'
                self.assertTrue(user_clip.is_file())
                user_clip.write_text('is_idle=0\n')
                result = subprocess.run([str(stage / 'usr/bin/domovushka'), '--check'], cwd=work,
                                        env=env, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(user_clip.read_text(), 'is_idle=0\n')
                self.assertFalse((work / 'settings.cfg').exists())
                self.assertIn('is_idle=1', (data / 'assets/starter/animations/humanoid/idle.cfg').read_text())
                notices = stage / 'usr/share/licenses/domovushka'
                self.assertTrue((notices / 'miniaudio.txt').is_file())
                self.assertTrue((module / 'vrm_overlay/qml/Main.qml').is_file())
            finally:
                for path in data.rglob('*'):
                    path.chmod(0o755 if path.is_dir() else 0o644)
