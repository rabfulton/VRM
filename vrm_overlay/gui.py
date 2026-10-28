"""domovushka's Qt Quick application, also available as python3 -m vrm_overlay.gui."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(prog='domovushka', description="domovushka — animated desktop companion")
    parser.add_argument("--window-backend", choices=("auto", "x11", "wayland"), default="auto")
    parser.add_argument("--socket", help="Renderer socket (default: private user runtime directory)")
    parser.add_argument("--settings", help="Renderer preferences file (default: XDG config directory)")
    parser.add_argument("--companion-socket", help="Private presentation socket for external MCP agents")
    source = parser.add_mutually_exclusive_group()
    source.add_argument('--external-agent', dest='external_agent', action='store_true', help='Use an external agent through MCP instead of built-in chat')
    source.add_argument('--builtin-chat', dest='external_agent', action='store_false', help='Use the basic built-in chat window')
    parser.set_defaults(external_agent=None)
    parser.add_argument("--attach", action="store_true", help="Connect only; leave the existing renderer running on exit")
    parser.add_argument("--model", help="Avatar to load when starting a new renderer")
    parser.add_argument("--animation-dir", help="Folder of VRMA/GLB animation files")
    parser.add_argument("--studio", action="store_true", help="Open Studio at launch (otherwise start with just the companion)")
    parser.add_argument("--check", action="store_true", help="Load QML and exit without connecting or starting a renderer")
    visibility = parser.add_mutually_exclusive_group()
    visibility.add_argument('--hide-avatar', action='store_true', help='Ask the running companion to hide')
    visibility.add_argument('--summon', action='store_true', help='Ask the running companion to appear')
    visibility.add_argument('--toggle-avatar', action='store_true', help='Toggle the running companion visibility')
    args = parser.parse_args()
    if args.hide_avatar or args.summon or args.toggle_avatar:
        from .companion import CompanionClient, companion_socket_path
        client = CompanionClient(args.companion_socket or companion_socket_path(args.socket))
        try:
            tool = 'avatar_toggle' if args.toggle_avatar else 'avatar_summon' if args.summon else 'avatar_hide'
            client.call(tool, {})
        except (OSError, ValueError, ConnectionError) as error:
            parser.exit(1, str(error) + '\n')
        finally:
            client.close()
        return 0
    try:
        from PySide6.QtCore import QTimer, QUrl
        from PySide6.QtGui import QGuiApplication, QIcon
        from PySide6.QtQml import QQmlApplicationEngine
        from PySide6.QtQuickControls2 import QQuickStyle
    except ImportError:
        parser.exit(1, "Install PySide6 (Arch: pyside6), or install requirements-gui.txt in your checkout environment.\n")
    from .qt_controller import Controller

    QQuickStyle.setStyle("Fusion")
    app = QGuiApplication([sys.argv[0]])
    # These storage identifiers retain existing QSettings, imported avatars and
    # cached previews. Visible branding and the desktop launcher have their own IDs.
    app.setOrganizationName("vrm-overlay")
    app.setApplicationName("VRM Studio")
    app.setApplicationDisplayName('domovushka')
    app.setDesktopFileName('domovushka')
    app.setWindowIcon(QIcon(str(Path(__file__).parent / 'thumbnails/starter.png')))
    app.setQuitOnLastWindowClosed(False)
    bridge = Controller(args.socket, attach=args.attach, model=args.model, animation_dir=args.animation_dir,
                        show_studio=args.studio, window_backend=args.window_backend, renderer_settings=args.settings,
                        external_agent=args.external_agent)
    companion = None
    if not args.check:
        from .companion import companion_socket_path
        from .qt_companion import CompanionServer
        try:
            companion = CompanionServer(bridge, args.companion_socket or companion_socket_path(bridge.socketPath, create=True))
            bridge.visibilityShortcut.start(args.companion_socket or companion_socket_path(bridge.socketPath))
        except (OSError, RuntimeError, ValueError) as error:
            bridge.shutdown()
            parser.exit(1, str(error) + '\n')
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
    if companion:
        app.aboutToQuit.connect(companion.close)
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
