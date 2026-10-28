"""Native/Python path agreement, legacy migration and immutable starter assets."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from vrm_overlay import paths
from vrm_overlay import avatar_catalog

ROOT = Path(__file__).resolve().parent.parent
PROBE = Path(os.environ.get('VRM_TEST_PATHS_PROBE', ROOT / 'build/tests/app-paths-probe'))


class PathTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix='domovushka-paths-')
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.runtime = self.root / 'runtime'
        self.runtime.mkdir(mode=0o700)
        self.environment = dict(HOME=str(self.root), XDG_CONFIG_HOME=str(self.root / 'config'),
                                XDG_DATA_HOME=str(self.root / 'data'), XDG_RUNTIME_DIR=str(self.runtime),
                                VRM_SETTINGS_PATH='')
        self.variables = patch.dict(os.environ, self.environment)
        self.variables.start()
        self.addCleanup(self.variables.stop)

    def native_paths(self, executable=PROBE, *args):
        if not executable.is_file():
            self.skipTest('Build the native path probe with make test')
        result = subprocess.run([str(executable), *args], capture_output=True, text=True, check=True)
        return result.stdout.splitlines()

    def test_native_and_python_defaults_and_permissions_agree(self):
        socket = paths.default_socket_path()
        self.assertEqual(self.native_paths(), [str(paths.settings_path()), socket])
        self.assertEqual(Path(socket).parent.stat().st_mode & 0o777, 0o700)
        self.assertEqual(paths.settings_path().parent.stat().st_mode & 0o777, 0o700)

    def test_empty_and_relative_xdg_values_use_home_defaults(self):
        for value in ('', 'relative/path'):
            with self.subTest(value=value), patch.dict(os.environ, XDG_CONFIG_HOME=value, XDG_DATA_HOME=value):
                expected = self.root / '.config/domovushka/renderer.cfg'
                self.assertEqual(paths.settings_path(), expected)
                self.assertEqual(paths.data_home(), self.root / '.local/share')
                self.assertEqual(self.native_paths()[0], str(expected))

    def test_runtime_fallback_and_hostile_child_are_consistent(self):
        with patch.dict(os.environ, XDG_RUNTIME_DIR=''):
            self.assertEqual(self.native_paths()[1], paths.default_socket_path())
        child = self.runtime / 'domovushka'
        child.symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, '0700'):
            paths.default_socket_path()
        if PROBE.is_file():
            self.assertNotEqual(subprocess.run([str(PROBE)], capture_output=True).returncode, 0)

    def test_migration_uses_binary_checkout_and_preserves_original_and_destination(self):
        if not PROBE.is_file():
            self.skipTest('Build the native path probe with make test')
        checkout = self.root / 'checkout'
        (checkout / 'src').mkdir(parents=True)
        (checkout / 'src/main.c').touch()
        (checkout / 'Makefile').touch()
        binary = checkout / 'vrm-overlay'
        shutil.copy2(PROBE, binary)
        legacy = checkout / 'settings.cfg'
        original = '# keep\nalways_on_top=0\nfuture.setting=retain\n'
        legacy.write_text(original)
        self.native_paths(binary, '--migrate')
        destination = paths.settings_path()
        self.assertEqual(destination.read_text(), original)
        self.assertEqual(legacy.read_text(), original)
        destination.write_text('camera_distance=4\n')
        self.native_paths(binary, '--migrate')
        self.assertEqual(destination.read_text(), 'camera_distance=4\n')
        self.assertEqual(legacy.read_text(), original)

    def test_launch_directory_settings_are_not_imported(self):
        (self.root / 'settings.cfg').write_text('always_on_top=0\n')
        if not PROBE.is_file():
            self.skipTest('Build the native path probe with make test')
        subprocess.run([str(PROBE), '--migrate'], cwd=self.root, check=True, capture_output=True)
        self.assertFalse(paths.settings_path().exists())

    def test_starter_copies_preserve_user_edits_across_upgrades(self):
        starter = self.root / 'packaged'
        library = starter / 'animations/humanoid'
        library.mkdir(parents=True)
        (starter / 'model.vrm').touch()
        (starter / 'manifest.json').write_text('{}')
        (starter / 'catalog.json').write_text(json.dumps(dict(avatars=[dict(name='Example', model='model.vrm',
            animations='animations/humanoid', rig='humanoid')])))
        (library / 'idle.vrma').write_bytes(b'motion')
        (library / 'idle.cfg').write_text('is_idle=1\n')
        with patch.object(avatar_catalog, 'STARTER', starter), patch.object(avatar_catalog, 'checkout_root', return_value=None):
            first = avatar_catalog.starter_avatars(writable=True)[0]
            destination = Path(first['animations'])
            self.assertTrue(destination.is_relative_to(paths.data_home()))
            self.assertEqual((destination / 'idle.vrma').read_bytes(), b'motion')
            (destination / 'idle.cfg').write_text('is_idle=0\nuser.setting=keep\n')
            (library / 'idle.cfg').write_text('is_idle=1\nloopable=1\n')
            (library / 'new.vrma').write_bytes(b'new motion')
            (starter / 'manifest.json').write_text('{"version":2}')
            self.assertEqual(avatar_catalog.starter_avatars(writable=True)[0]['animations'], str(destination))
            self.assertEqual((destination / 'idle.cfg').read_text(), 'is_idle=0\nuser.setting=keep\n')
            self.assertEqual((destination / 'new.vrma').read_bytes(), b'new motion')
            self.assertTrue((destination / '.packaged-version').is_file())
            self.assertEqual((library / 'idle.cfg').read_text(), 'is_idle=1\nloopable=1\n')
