#!/usr/bin/env python3
"""Real-display package check with isolated user files; no speech downloads/API calls."""
import argparse
import os
from pathlib import Path
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path('/usr'), help='Installed prefix or extracted package /usr')
    parser.add_argument('--backend', choices=('x11', 'wayland'), default='x11')
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    modules = list(prefix.glob(f'lib/python{sys.version_info.major}.{sys.version_info.minor}/site-packages'))
    if not modules or not (modules[0] / 'vrm_overlay').is_dir():
        parser.error('No domovushka Python module for this interpreter under the supplied prefix')
    sys.path.insert(0, str(modules[0]))
    os.environ.update(QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='basic', QT_QUICK_BACKEND='software',
                      VRM_DATA_DIR=str(prefix / 'share/domovushka'), VRM_RENDERER=str(prefix / 'bin/vrm-overlay'))
    os.environ.pop('VRM_SETTINGS_PATH', None)
    from PySide6.QtCore import QProcess, QUrl
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQml import QQmlApplicationEngine
    from PySide6.QtQuickControls2 import QQuickStyle
    from PySide6.QtTest import QTest
    import vrm_overlay
    from vrm_overlay.animation_config import save_properties, load_properties, load_access_properties
    from vrm_overlay.paths import settings_path
    from vrm_overlay.qt_controller import Controller

    assert Path(vrm_overlay.__file__).is_relative_to(prefix), vrm_overlay.__file__
    QQuickStyle.setStyle('Fusion')
    app = QGuiApplication([])
    app.setOrganizationName('vrm-overlay')
    app.setApplicationName('VRM Studio')
    app.setQuitOnLastWindowClosed(False)
    with tempfile.TemporaryDirectory(prefix='domovushka-installed-') as directory:
        root = Path(directory)
        runtime = root / 'run'
        runtime.mkdir(mode=0o700)
        os.environ.update(XDG_CONFIG_HOME=str(root / 'config'), XDG_DATA_HOME=str(root / 'data'),
                          XDG_CACHE_HOME=str(root / 'cache'), XDG_RUNTIME_DIR=str(runtime),
                          VRM_SPEECH_HOME=str(root / 'speech'))
        expected_floating = None
        edited = None
        for run in range(2):
            bridge = Controller(window_backend=args.backend)
            engine = QQmlApplicationEngine()
            engine.rootContext().setContextProperty('bridge', bridge)
            engine.load(QUrl.fromLocalFile(str(modules[0] / 'vrm_overlay/qml/Main.qml')))
            assert engine.rootObjects(), 'Installed QML did not load'

            def wait(predicate, timeout=30):
                deadline = time.monotonic() + timeout
                while not predicate():
                    if time.monotonic() >= deadline:
                        raise AssertionError(f'{bridge.lastError}\n{bridge.logText}')
                    QTest.qWait(10)

            def request(command, label):
                replies, errors = [], []
                bridge._queue([command], label, done=replies.append, failed=errors.append)
                wait(lambda: bool(replies or errors))
                assert not errors, errors
                return replies[0]

            try:
                bridge.connectRuntime()
                wait(lambda: bridge.connected and bridge.state['model_loaded'] and bridge.state['playing'], timeout=60)
                assert bridge.state['window_backend'] == args.backend, bridge.state
                bounds = request(dict(action='get_frame_bounds'), 'Installed pixels')['data']
                pixels = bounds['visible_pixels']
                assert pixels > 100, bounds
                assert Path(bridge.animationFolder).is_relative_to(root / 'data')
                if run == 0:
                    edited = Path(bridge.animationFolder) / 'idle.vrma'
                    save_properties(edited, dict(load_properties(edited), llm_enabled=True))
                    expected_floating = not bridge.state['always_on_top']
                    request(dict(action='set_render', always_on_top=expected_floating), 'Remember floating')
                    assert f'always_on_top={int(expected_floating)}' in settings_path().read_text()
                else:
                    assert bridge.state['always_on_top'] == expected_floating
                    assert load_access_properties(edited)['llm_enabled']
                bridge.quitApplication()
                wait(lambda: bridge._process.state() == QProcess.NotRunning)
                print(f'Installed {args.backend} launch {run + 1}: {pixels} pixels; persistence and Quit passed')
            finally:
                del engine
                bridge.shutdown()
                del bridge
        assert not (root / 'speech').exists(), 'Speech downloaded without being requested'


if __name__ == '__main__':
    main()
