"""Launcher behavior across working directories and checkout environments."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class LauncherTests(unittest.TestCase):
    def test_executable_help_works_outside_checkout(self):
        with tempfile.TemporaryDirectory() as work:
            result = subprocess.run([str(ROOT / 'domovushka'), '--help'], cwd=work,
                                    capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('usage: domovushka', result.stdout)
        self.assertIn('--studio', result.stdout)

    def test_checkout_environment_preserves_arguments_and_working_directory(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work) / 'install with spaces'
            interpreter = root / '.venv/bin/python'
            interpreter.parent.mkdir(parents=True)
            interpreter.write_text('#!/bin/sh\npwd\nprintf \'%s\\n\' "$@"\n')
            interpreter.chmod(0o755)
            launcher = root / 'domovushka'
            shutil.copyfile(ROOT / 'domovushka', launcher)
            cwd = Path(work) / 'launch directory'
            cwd.mkdir()
            args = ['--model', 'avatars/my avatar.vrm', '--studio', '--socket', '/tmp/a socket.sock']
            result = subprocess.run([sys.executable, str(launcher), *args], cwd=cwd,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(), [str(cwd), str(launcher), *args])

    @unittest.skipUnless(importlib.util.find_spec('PySide6'), 'Install requirements-gui.txt for QML checks')
    def test_new_and_compatibility_launchers_load_qml_without_starting_renderer(self):
        with tempfile.TemporaryDirectory() as work:
            environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='basic',
                               QT_QUICK_BACKEND='software', XDG_CONFIG_HOME=work + '/config',
                               XDG_DATA_HOME=work + '/data', XDG_CACHE_HOME=work + '/cache')
            for launcher in ('domovushka', 'control.py'):
                with self.subTest(launcher=launcher):
                    result = subprocess.run([sys.executable, str(ROOT / launcher), '--check'], cwd=work,
                                            env=environment, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
