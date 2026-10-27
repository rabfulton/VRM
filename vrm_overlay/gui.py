"""domovushka's Qt Quick application, also available as python3 -m vrm_overlay.gui."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(prog='domovushka', description="domovushka — animated desktop companion")
    parser.add_argument("--window-backend", choices=("auto", "x11", "wayland"), default="auto")
    parser.add_argument("--socket", default="/tmp/vrm-overlay.sock")
    parser.add_argument("--attach", action="store_true", help="Connect only; leave the existing renderer running on exit")
    parser.add_argument("--model", help="Avatar to load when starting a new renderer")
    parser.add_argument("--animation-dir", help="Folder of VRMA/GLB animation files")
    parser.add_argument("--studio", action="store_true", help="Open Studio at launch (otherwise start with just the companion)")
    parser.add_argument("--check", action="store_true", help="Load QML and exit without connecting or starting a renderer")
    args = parser.parse_args()
    try:
        from PySide6.QtCore import QTimer, QUrl
        from PySide6.QtGui import QGuiApplication
        from PySide6.QtQml import QQmlApplicationEngine
        from PySide6.QtQuickControls2 import QQuickStyle
    except ImportError:
        parser.exit(1, "Install PySide6: python3 -m pip install -r requirements-gui.txt\n")
    from .qt_controller import Controller

    QQuickStyle.setStyle("Fusion")
    app = QGuiApplication([sys.argv[0]])
    # These storage identifiers retain existing QSettings, imported avatars and
    # cached previews. Visible branding and the desktop launcher have their own IDs.
    app.setOrganizationName("vrm-overlay")
    app.setApplicationName("VRM Studio")
    app.setApplicationDisplayName('domovushka')
    app.setDesktopFileName('domovushka')
    app.setQuitOnLastWindowClosed(False)
    bridge = Controller(args.socket, attach=args.attach, model=args.model, animation_dir=args.animation_dir,
                        show_studio=args.studio, window_backend=args.window_backend)
    # QGuiApplication.quit() asks visible windows to close. Studio intentionally
    # rejects ordinary close events to hide itself, which can veto that request
    # after the renderer has already stopped. Explicit companion Quit must end
    # the event loop; aboutToQuit below still performs all worker/child cleanup.
    bridge.quitRequested.connect(lambda: app.exit(0))
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("bridge", bridge)
    engine.load(QUrl.fromLocalFile(str(Path(__file__).parent / "qml" / "Main.qml")))
    if not engine.rootObjects():
        return 1
    app.aboutToQuit.connect(bridge.shutdown)
    if args.check:
        QTimer.singleShot(100, lambda: app.exit(0))
    else:
        QTimer.singleShot(0, bridge.connectRuntime)
    result = app.exec()
    # Tear QML down before its context object so bindings remain valid during exit.
    del engine
    return result


if __name__ == "__main__":
    sys.exit(main())
