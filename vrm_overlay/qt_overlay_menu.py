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
        if visible:
            self._grab_timer.start(0)
        else:
            self._grab_timer.stop()
            if self._native_input:
                self._window.setMouseGrabEnabled(False)

    def _grab(self):
        if self._window.isVisible() and self._window.isExposed() and self._native_input:
            # Catch outside presses even when they target another application.
            # Keyboard focus stays with the menu through requestActivate().
            self._window.setMouseGrabEnabled(True)

    def eventFilter(self, watched, event):
        if watched is self._window and self._window.isVisible():
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
