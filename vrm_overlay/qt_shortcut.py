"""Asynchronous lifecycle and settings for the isolated shortcut helper."""
import json
from pathlib import Path
import sys

from PySide6.QtCore import QObject, Property, QProcess, QTimer, Signal, Slot

from .visibility_shortcut import parse_shortcut


class VisibilityShortcut(QObject):
    changed = Signal()

    def __init__(self, runtime):
        super().__init__(runtime)
        self.runtime = runtime
        self.value = str(runtime._settings.value('avatar/shortcut', ''))
        self.message = 'No global shortcut assigned.'
        self.socket_path = None
        self.process = None
        self.buffer = bytearray()
        self.closing = False

    sequence = Property(str, lambda self: self.value, notify=changed)
    status = Property(str, lambda self: self.message, notify=changed)

    def start(self, socket_path):
        self.socket_path = socket_path
        self.apply(self.value)

    @Slot(str)
    def apply(self, value):
        value = value.strip()
        try:
            if value:
                value, _, _ = parse_shortcut(value)
        except ValueError as error:
            self.message = str(error)
            self.changed.emit()
            return
        self.value = value
        self.runtime._settings.setValue('avatar/shortcut', value)
        self.message = 'Registering shortcut…' if value else 'No global shortcut assigned.'
        self.changed.emit()
        if self.process and self.process.state() != QProcess.NotRunning:
            old = self.process
            old.setProperty('replaced', True)
            old.terminate()
            deadline = QTimer(old)
            deadline.setSingleShot(True)
            deadline.timeout.connect(old.kill)
            old.finished.connect(deadline.stop)
            deadline.start(1500)
        else:
            self._launch()

    def _launch(self):
        if self.closing or not self.value or not self.socket_path:
            return
        process = QProcess(self)
        self.process = process
        self.buffer.clear()
        process.setWorkingDirectory(str(Path(__file__).resolve().parent.parent))
        process.setProgram(sys.executable)
        process.setArguments(['-m', 'vrm_overlay.visibility_shortcut', self.value,
                              '--companion-socket', self.socket_path])
        process.readyReadStandardOutput.connect(self._read)
        process.finished.connect(lambda *_args: self._finished(process))
        process.errorOccurred.connect(lambda error: self._error(process.errorString()) if error == QProcess.FailedToStart else None)
        process.start()

    def _finished(self, process):
        if self.process is not process:
            return
        self.process = None
        if process.property('replaced'):
            self._launch()
        elif not self.closing and self.value and not self.message.startswith('Shortcut error:'):
            self._error('Shortcut helper stopped. Apply the shortcut again to register it.')
        process.deleteLater()

    def _error(self, message):
        self.message = 'Shortcut error: ' + message
        self.changed.emit()

    def _read(self):
        self.buffer.extend(bytes(self.process.readAllStandardOutput()))
        if len(self.buffer) > 16384:
            self._error('Invalid shortcut helper response.')
            self.process.terminate()
            return
        while b'\n' in self.buffer:
            line, _, remaining = self.buffer.partition(b'\n')
            self.buffer = bytearray(remaining)
            try:
                message = json.loads(line)
                if message['event'] == 'ready':
                    self.message = self.value + ' active on ' + message['backend']
                    self.changed.emit()
                elif message['event'] == 'activated':
                    self.runtime.toggleAvatar()
                elif message['event'] == 'error':
                    self._error(message['message'])
            except (ValueError, KeyError, TypeError):
                self._error('Invalid shortcut helper response.')

    def shutdown(self):
        self.closing = True
        process = self.process
        if process and process.state() != QProcess.NotRunning:
            process.closeWriteChannel()
            if not process.waitForFinished(3500):
                process.kill()
                process.waitForFinished(500)
