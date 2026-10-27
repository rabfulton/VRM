"""Cancellable Chat Completions SSE transport using Qt's existing network stack."""
from __future__ import annotations

import json

from PySide6.QtCore import QObject, QTimer, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from .chat import CompletionStream, StreamError
from .credentials import normalize_endpoint


class ChatTransport(QObject):
    text = Signal(str)
    completed = Signal()
    failed = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._manager = QNetworkAccessManager(self)
        self._reply = None
        self._parser = None
        self._deadline = QTimer(self)
        self._deadline.setSingleShot(True)
        self._deadline.timeout.connect(lambda: self._fail("Chat request timed out. You can try again."))

    def start(self, endpoint, model, messages, api_key):
        self.cancel()
        endpoint = normalize_endpoint(endpoint)
        if any(c in api_key for c in "\r\n\0"):
            raise ValueError("The stored API key is invalid; replace it in Chat settings.")
        request = QNetworkRequest(QUrl(endpoint))
        request.setHeader(QNetworkRequest.ContentTypeHeader, "application/json")
        request.setRawHeader(b"Accept", b"text/event-stream")
        request.setAttribute(QNetworkRequest.RedirectPolicyAttribute, QNetworkRequest.ManualRedirectPolicy)
        request.setTransferTimeout(60000)
        if api_key:
            request.setRawHeader(b"Authorization", ("Bearer " + api_key).encode())
        payload = json.dumps(dict(model=model, messages=messages, stream=True), allow_nan=False).encode()
        self._parser = CompletionStream()
        reply = self._manager.post(request, payload)
        self._reply = reply
        reply.setReadBufferSize(65536)
        reply.readyRead.connect(lambda: self._read(reply))
        reply.finished.connect(lambda: self._finished(reply))
        self._deadline.start(180000)

    def _valid_response(self, reply):
        status = reply.attribute(QNetworkRequest.HttpStatusCodeAttribute)
        if status is not None and not 200 <= status < 300:
            descriptions = {401: "Check the API key.", 403: "This API key cannot access the model.",
                            404: "Check the endpoint and model ID.", 429: "Rate limit or quota reached."}
            self._fail(f"Chat request failed (HTTP {status}). " + descriptions.get(status, "Check the server configuration."))
            return False
        if status is not None:
            content_type = str(reply.header(QNetworkRequest.ContentTypeHeader) or "").lower()
            if not content_type.startswith("text/event-stream"):
                self._fail("The server did not return a streaming response. Use a Chat Completions endpoint with SSE support.")
                return False
        return status is not None

    def _read(self, reply):
        if reply is not self._reply or not self._valid_response(reply):
            return
        try:
            for text in self._parser.feed(bytes(reply.readAll())):
                if reply is not self._reply:
                    return
                self.text.emit(text)
            if reply is self._reply and self._parser.done:
                self._succeeded()
        except StreamError as error:
            self._fail(str(error))

    def _finished(self, reply):
        if reply is not self._reply:
            return
        if not self._valid_response(reply):
            if reply is self._reply:
                self._fail("Could not connect to the chat API. Check the URL, network and TLS configuration.")
            return
        if reply.error() != QNetworkReply.NoError:
            # Do not display bodies or error strings that could echo credentials.
            self._fail("The chat connection was interrupted. The partial reply was not added to conversation history.")
            return
        self._read(reply)
        if reply is not self._reply:
            return
        try:
            for text in self._parser.feed(b"", final=True):
                self.text.emit(text)
            self._succeeded()
        except StreamError as error:
            self._fail(str(error))

    def _succeeded(self):
        self.cancel()
        self.completed.emit()

    def _fail(self, message):
        self.cancel()
        self.failed.emit(message)

    def cancel(self):
        self._deadline.stop()
        reply, self._reply = self._reply, None
        if reply is not None:
            reply.abort()
            reply.deleteLater()
