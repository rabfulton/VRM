"""Input dismissal for the standalone overlay menu window."""
from PySide6.QtCore import QEvent, QObject, QRectF, QTimer, Qt
from PySide6.QtGui import QGuiApplication


class OverlayMenuInput(QObject):
    def __init__(self, window):
        super().__init__(window)
        self._window = window
        self._native_input = QGuiApplication.platformName() not in ('offscreen', 'minimal')
        self._grab_timer = QTimer(self)
        self._grab_timer.setSingleShot(True)
        self._grab_timer.timeout.connect(self._grab)
        window.installEventFilter(self)
        window.visibleChanged.connect(self._visibility_changed)

    def _visibility_changed(self, visible):
        if getattr(self,'_window',None) is None:return
        if visible:
            self._grab_timer.start(0)
        else:
            self._grab_timer.stop()
            if self._native_input:
                self._window.setMouseGrabEnabled(False)

    def _grab(self):
        window=getattr(self,'_window',None)
        if window is not None and window.isVisible() and window.isExposed() and self._native_input:
            # Catch outside presses even when they target another application.
            # Keyboard focus stays with the menu through requestActivate().
            self._window.setMouseGrabEnabled(True)

    def eventFilter(self, watched, event):
        # Qt can deliver teardown events while a Python-owned filter's wrapper
        # is being finalized. Do not inspect half-destroyed Python state.
        window=getattr(self,'_window',None)
        if window is None:return False
        if watched is window and window.isVisible():
            if event.type() == QEvent.Expose:
                self._grab_timer.start(0)
            elif event.type() == QEvent.MouseButtonPress:
                rect = QRectF(0, 0, self._window.width(), self._window.height())
                if event.button() == Qt.RightButton or not rect.contains(event.position()):
                    self._window.hide()
                    return True  # Consume cancellation; don't trigger an avatar action.
            elif event.type() == QEvent.KeyPress and event.key() == Qt.Key_Escape:
                self._window.hide()
                return True
        return super().eventFilter(watched, event)

    def close(self):
        window=getattr(self,'_window',None)
        if window is None:return
        try:
            self._grab_timer.stop()
            window.removeEventFilter(self)
            if self._native_input:window.setMouseGrabEnabled(False)
        except RuntimeError:
            pass  # The QML engine may already have destroyed the window.
        self._window=None
