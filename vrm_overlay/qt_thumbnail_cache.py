"""Persistent, bounded image previews fetched asynchronously by Qt."""
import hashlib
from pathlib import Path

from PySide6.QtCore import QBuffer, QIODevice, QObject, QSaveFile, QSize, QUrl, Qt, Signal
from PySide6.QtGui import QImageReader
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest


class ThumbnailCache(QObject):
    changed = Signal()
    MAX_BYTES = 8 * 1024 * 1024

    def __init__(self, root, parent=None):
        super().__init__(parent)
        self.root = Path(root).resolve()
        self._network = QNetworkAccessManager(self)
        self._pending = {}
        self._closed = False

    def _path(self, url):
        return self.root / (hashlib.sha256(url.encode()).hexdigest() + '.png')

    def localUrl(self, value):
        url = QUrl(value)
        if url.scheme() not in ('http', 'https'):
            return value
        path = self._path(value)
        return path.as_uri() if path.is_file() and QImageReader(str(path)).canRead() else ''

    def ensure(self, urls):
        if self._closed:
            return
        for value in urls:
            url = QUrl(value)
            if url.scheme() not in ('http', 'https') or self.localUrl(value) or value in self._pending:
                continue
            request = QNetworkRequest(url)
            request.setTransferTimeout(15000)
            request.setRawHeader(b'Accept', b'image/png,image/jpeg,image/webp')
            reply = self._network.get(request)
            reply.setReadBufferSize(self.MAX_BYTES + 1)
            self._pending[value] = (reply, bytearray())
            reply.readyRead.connect(lambda value=value: self._read(value))
            reply.finished.connect(lambda value=value: self._complete(value))

    def _read(self, value):
        pending = self._pending.get(value)
        if pending:
            reply, data = pending
            if reply.isOpen():
                data.extend(bytes(reply.readAll()))
            if len(data) > self.MAX_BYTES and reply.isRunning():
                reply.abort()

    def _complete(self, value):
        self._read(value)
        pending = self._pending.pop(value, None)
        if not pending:
            return
        reply, data = pending
        status = reply.attribute(QNetworkRequest.HttpStatusCodeAttribute)
        success = (not self._closed and reply.error() == QNetworkReply.NoError and
                   status == 200 and 0 < len(data) <= self.MAX_BYTES)
        reply.deleteLater()
        if not success:
            return  # Leave the placeholder; a later dialog opening can retry.
        buffer = QBuffer()
        buffer.setData(bytes(data))
        buffer.open(QIODevice.ReadOnly)
        reader = QImageReader(buffer)
        size = reader.size()
        if not size.isValid() or max(size.width(), size.height()) > 8192 or size.width() * size.height() > 32_000_000:
            return
        reader.setAutoTransform(True)
        reader.setScaledSize(size.scaled(QSize(480, 480), Qt.KeepAspectRatio))
        image = reader.read()
        if image.isNull():
            return
        try:
            self.root.mkdir(parents=True, exist_ok=True)
        except OSError:
            return
        output = QSaveFile(str(self._path(value)))
        if not output.open(QIODevice.WriteOnly):
            return
        if not image.save(output, 'PNG'):
            output.cancelWriting()
            return
        if output.commit():
            self.changed.emit()

    def shutdown(self):
        self._closed = True
        for reply, _data in list(self._pending.values()):
            reply.abort()
