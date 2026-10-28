#!/usr/bin/env python3
"""Manual Xorg integration: global grab, repeat suppression, conflict and release.

Uses a temporary Ctrl+Alt+Shift+Y grab and XTest events. No renderer or user
settings are touched. Run with python3 tests/shortcut_smoke.py on Xorg.
"""
import ctypes as C
import os
from pathlib import Path
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QCoreApplication, QObject, QSettings
from PySide6.QtTest import QTest
from vrm_overlay.qt_shortcut import VisibilityShortcut


def main():
    if os.environ.get('WAYLAND_DISPLAY') or os.environ.get('XDG_SESSION_TYPE') == 'wayland':
        raise SystemExit('This XTest smoke check requires an Xorg session.')
    app = QCoreApplication([])
    class Runtime(QObject):
        def __init__(self, path):
            super().__init__()
            self._settings = QSettings(path, QSettings.IniFormat)
            self.activations = 0

        def toggleAvatar(self):
            self.activations += 1

    with tempfile.TemporaryDirectory() as directory:
        runtime = Runtime(directory + '/settings.ini')
        shortcut = VisibilityShortcut(runtime)
        conflict = VisibilityShortcut(Runtime(directory + '/second.ini'))
        def wait(predicate):
            deadline = time.monotonic() + 5
            while not predicate():
                assert time.monotonic() < deadline, (shortcut.status, conflict.status)
                QTest.qWait(10)
        x = C.CDLL('libX11.so.6')
        xt = C.CDLL('libXtst.so.6')
        x.XOpenDisplay.restype = C.c_void_p
        x.XOpenDisplay.argtypes = [C.c_char_p]
        x.XStringToKeysym.restype = C.c_ulong
        x.XStringToKeysym.argtypes = [C.c_char_p]
        x.XKeysymToKeycode.restype = C.c_ubyte
        x.XKeysymToKeycode.argtypes = [C.c_void_p, C.c_ulong]
        x.XSync.argtypes = [C.c_void_p, C.c_int]
        x.XCloseDisplay.argtypes = [C.c_void_p]
        xt.XTestFakeKeyEvent.argtypes = [C.c_void_p, C.c_uint, C.c_int, C.c_ulong]
        display = x.XOpenDisplay(None)
        assert display
        keys = [x.XKeysymToKeycode(display, x.XStringToKeysym(name))
                for name in (b'Control_L', b'Alt_L', b'Shift_L', b'y')]
        def event(code, pressed):
            assert xt.XTestFakeKeyEvent(display, code, pressed, 0)
            x.XSync(display, False)
        try:
            shortcut.start(directory + '/companion.sock')
            shortcut.apply('Ctrl+Alt+Shift+Y')
            wait(lambda: 'active on X11' in shortcut.status or 'error:' in shortcut.status)
            assert 'active on X11' in shortcut.status, shortcut.status
            conflict.start(directory + '/companion.sock')
            conflict.apply('Ctrl+Alt+Shift+Y')
            wait(lambda: 'error:' in conflict.status)
            assert 'already in use' in conflict.status, conflict.status
            for key in keys:
                event(key, True)
            wait(lambda: runtime.activations == 1)
            for _ in range(5):
                event(keys[-1], True)
            QTest.qWait(150)
            assert runtime.activations == 1, runtime.activations
            for key in reversed(keys):
                event(key, False)
            for key in keys:
                event(key, True)
            wait(lambda: runtime.activations == 2)
            for key in reversed(keys):
                event(key, False)
            shortcut.apply('Ctrl+Alt+Shift+U')
            wait(lambda: 'active on X11' in shortcut.status)
            keys[-1] = x.XKeysymToKeycode(display, x.XStringToKeysym(b'u'))
            for key in keys:
                event(key, True)
            wait(lambda: runtime.activations == 3)
            for key in reversed(keys):
                event(key, False)
            shortcut.apply('')
            wait(lambda: shortcut.process is None)
            conflict.apply('Ctrl+Alt+Shift+Y')
            wait(lambda: 'active on X11' in conflict.status)
            assert 'active on X11' in conflict.status
            print('X11 global shortcut: activation, held-key repeat, conflict, rebind, clear and re-registration passed.')
        finally:
            for key in reversed(keys):
                event(key, False)
            shortcut.shutdown()
            conflict.shutdown()
            x.XCloseDisplay(display)
    return 0


if __name__ == '__main__':
    sys.exit(main())
