"""Private multi-agent listener; the Qt controller still owns renderer IPC."""
from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import uuid

from PySide6.QtCore import QObject, QLockFile
from PySide6.QtNetwork import QLocalServer

from .companion import MAX_MESSAGE, MAX_RESPONSE, validate_call


class CompanionServer(QObject):
    def __init__(self, runtime, path):
        super().__init__(runtime)
        self.runtime, self.path = runtime, str(path)
        self.peers = {}
        if len(os.fsencode(self.path)) >= 108:
            raise RuntimeError('Companion socket path is too long.')
        parent = Path(path).parent
        info = parent.stat()
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise RuntimeError('Companion socket parent must be your private directory with mode 0700.')
        self.lock = QLockFile(self.path + '.lock')
        if not self.lock.tryLock(0):
            raise RuntimeError('Another companion already owns this presentation socket.')
        self.server = QLocalServer(self)
        try:
            if os.path.lexists(self.path):
                info = os.lstat(self.path)
                if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
                    raise RuntimeError('Refusing to replace a non-socket or another user\'s socket.')
                QLocalServer.removeServer(self.path)
            self.server.setSocketOptions(QLocalServer.UserAccessOption)
            if not self.server.listen(self.path):
                raise RuntimeError('Cannot listen on the companion presentation socket.')
            os.chmod(self.path, 0o600)
            self.server.newConnection.connect(self.accept)
        except Exception:
            self.server.close()
            self.lock.unlock()
            raise

    def accept(self):
        while self.server.hasPendingConnections():
            peer = self.server.nextPendingConnection()
            if len(self.peers) >= 8:
                peer.abort()
                peer.deleteLater()
                continue
            self.peers[peer] = dict(owner=uuid.uuid4().hex, buffer=bytearray())
            peer.setReadBufferSize(MAX_MESSAGE)
            peer.readyRead.connect(lambda peer=peer: self.read(peer))
            peer.disconnected.connect(lambda peer=peer: self.disconnect(peer))

    def disconnect(self, peer):
        state = self.peers.pop(peer, None)
        if state is not None:
            self.runtime.presentations.cancel_owner(state['owner'])
            if hasattr(self.runtime, 'walking'):self.runtime.walking.cancel_owner(state['owner'])
        peer.deleteLater()

    def read(self, peer):
        state = self.peers.get(peer)
        if state is None:
            return
        state['buffer'].extend(bytes(peer.readAll()))
        if len(state['buffer']) > MAX_MESSAGE:
            peer.abort()
            return
        while b'\n' in state['buffer']:
            line, _, rest = state['buffer'].partition(b'\n')
            state['buffer'] = bytearray(rest)
            request_id = None
            try:
                request = json.loads(line)
                if not isinstance(request, dict) or type(request.get('id')) is not int:
                    raise ValueError('Request requires an integer ID.')
                request_id = request['id']
                name, arguments = request.get('tool'), request.get('arguments')
                validate_call(name, arguments)
                manager = self.runtime.presentations
                if name == 'avatar_visibility':
                    result = self.runtime.visibility.status()
                elif name in ('avatar_hide', 'avatar_summon', 'avatar_toggle'):
                    visible = not self.runtime.visibility.wanted if name == 'avatar_toggle' else name == 'avatar_summon'
                    result = self.runtime.visibility.change(visible)
                elif name == 'avatar_capabilities':
                    result = manager.capabilities()
                elif name == 'avatar_walk':
                    result = self.runtime.walking.start(state['owner'], **arguments)
                elif name == 'avatar_motion_status':
                    result = self.runtime.walking.status(state['owner'], arguments['motion_id'])
                elif name == 'avatar_motion_cancel':
                    result = self.runtime.walking.cancel(state['owner'], arguments['motion_id'])
                elif name == 'avatar_status':
                    result = manager.status(state['owner'], arguments['presentation_id'])
                elif name == 'avatar_cancel':
                    result = manager.cancel(state['owner'], arguments['presentation_id'])
                else:
                    result = manager.submit(state['owner'], **arguments)
                response = dict(id=request_id, result=result)
            except (ValueError, TypeError, KeyError, RecursionError) as error:
                response = dict(id=request_id, error=str(error)[:512])
            payload = json.dumps(response, allow_nan=False).encode() + b'\n'
            if len(payload) > MAX_RESPONSE or peer.bytesToWrite() + len(payload) > MAX_RESPONSE:
                peer.abort()
                return
            peer.write(payload)

    def close(self):
        self.server.close()
        for peer in list(self.peers):
            self.runtime.presentations.cancel_owner(self.peers[peer]['owner'])
            if hasattr(self.runtime, 'walking'):self.runtime.walking.cancel_owner(self.peers[peer]['owner'])
            peer.abort()
        self.lock.unlock()
