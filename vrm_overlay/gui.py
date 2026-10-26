"""Launch the Qt Quick controller: python3 -m vrm_overlay.gui."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description="VRM Studio — Qt Quick avatar controller")
    parser.add_argument("--socket", default="/tmp/vrm-overlay.sock")
    parser.add_argument("--attach", action="store_true", help="Connect only; leave the existing renderer running on exit")
    parser.add_argument("--model", help="Avatar to load when starting a new renderer")
    parser.add_argument("--animation-dir", help="Folder of VRMA/GLB animation files")
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
    app.setOrganizationName("vrm-overlay")
    app.setApplicationName("VRM Studio")
    bridge = Controller(args.socket, attach=args.attach, model=args.model, animation_dir=args.animation_dir)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("bridge", bridge)
    engine.load(QUrl.fromLocalFile(str(Path(__file__).parent / "qml" / "Main.qml")))
    if not engine.rootObjects():
        return 1
    app.aboutToQuit.connect(bridge.shutdown)
    if args.check:
        QTimer.singleShot(100, app.quit)
    else:
        QTimer.singleShot(0, bridge.connectRuntime)
    result = app.exec()
    # Tear QML down before its context object so bindings remain valid during exit.
    del engine
    return result


if __name__ == "__main__":
    sys.exit(main())
