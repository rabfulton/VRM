"""Synchronous, thread-safe NDJSON client with bounded requests and deadlines."""
from __future__ import annotations

import base64
import json
import socket
import threading
import time

DEFAULT_SOCKET_PATH = "/tmp/vrm-overlay.sock"
MAX_REQUEST_BYTES = 65536
MAX_RESPONSE_BYTES = 1024 * 1024


class IpcError(RuntimeError):
    """The runtime rejected a command. The connection remains usable."""

    def __init__(self, response: dict):
        self.response = response
        error = response.get("error") or {}
        super().__init__(error.get("message", "Overlay command failed"))


class IpcClient:
    """One connection, serialized commands, and no automatic replay of side effects.

    A failed/timed-out transport is closed; the next request can reconnect. A timeout
    leaves the command's outcome unknown, so callers must not blindly retry it.
    """

    def __init__(self, socket_path: str = DEFAULT_SOCKET_PATH):
        self.socket_path = socket_path
        self._lock = threading.Lock()
        self._sock: socket.socket | None = None
        self._recv_buf = bytearray()
        self._next_id = 1

    def _connect(self, timeout_s: float) -> None:
        if self._sock is not None:
            return
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.settimeout(timeout_s)
            sock.connect(self.socket_path)
        except BaseException:
            sock.close()
            raise
        self._sock = sock

    def connect(self, timeout_s: float = 2.0) -> None:
        with self._lock:
            self._connect(timeout_s)

    def _close(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None
        self._recv_buf.clear()

    def close(self) -> None:
        with self._lock:
            self._close()

    def __enter__(self) -> IpcClient:
        self.connect()
        return self

    def __exit__(self, *_args) -> None:
        self.close()

    def request(self, payload: dict, timeout_s: float = 2.0) -> dict:
        if timeout_s <= 0:
            raise ValueError("timeout_s must be positive")
        with self._lock:
            command = dict(payload)
            command.setdefault("id", self._next_id)
            expect_id = command["id"]
            if type(expect_id) is not int or not -(2**31) <= expect_id < 2**31:
                raise ValueError("id must be a signed 32-bit integer")
            self._next_id = max(self._next_id, expect_id + 1)
            if self._next_id >= 2**31:
                self._next_id = 1
            data = (json.dumps(command, allow_nan=False) + "\n").encode("utf-8")
            if len(data) > MAX_REQUEST_BYTES:
                raise ValueError(f"IPC request exceeds {MAX_REQUEST_BYTES} bytes; split audio into chunks")
            deadline = time.monotonic() + timeout_s

            def remaining() -> float:
                value = deadline - time.monotonic()
                if value <= 0:
                    raise TimeoutError(f"No IPC response for {command.get('action')} (id={expect_id})")
                return value

            try:
                self._connect(remaining())
                assert self._sock is not None
                self._sock.settimeout(remaining())
                self._sock.sendall(data)
                while True:
                    if b"\n" in self._recv_buf:
                        line, _, rest = self._recv_buf.partition(b"\n")
                        self._recv_buf = bytearray(rest)
                        if len(line) > MAX_RESPONSE_BYTES:
                            raise ValueError("IPC response too large")
                        if not line:
                            continue
                        response = json.loads(line)
                        if not isinstance(response, dict):
                            raise ValueError("IPC response must be an object")
                        if response.get("id") != expect_id:
                            # Preserve framing even if a peer sends an unrelated reply.
                            remaining()
                            continue
                        if response.get("status") == "error":
                            raise IpcError(response)
                        if response.get("status") != "ok":
                            raise ValueError("Invalid IPC response status")
                        return response
                    if len(self._recv_buf) > MAX_RESPONSE_BYTES:
                        raise ValueError("IPC response too large")
                    self._sock.settimeout(remaining())
                    chunk = self._sock.recv(65536)
                    if not chunk:
                        raise ConnectionError("Overlay disconnected")
                    self._recv_buf.extend(chunk)
            except IpcError:
                raise
            except BaseException:
                self._close()
                raise

    def send_only(self, payload: dict, timeout_s: float = 5.0) -> None:
        """Compatibility helper: consume and validate the acknowledgement."""
        self.request(payload, timeout_s)

    def audio_stream_begin(self) -> None:
        self.request({"action": "audio_stream_begin"})

    def audio_stream_chunk(self, pcm_bytes: bytes) -> None:
        if len(pcm_bytes) % 2:
            raise ValueError("PCM must contain complete 16-bit samples")
        self.request({"action": "audio_stream_chunk", "data": base64.b64encode(pcm_bytes).decode("ascii")})

    def audio_stream_end(self) -> None:
        self.request({"action": "audio_stream_end"})

    def audio_cancel(self) -> None:
        self.request({"action": "audio_cancel"})

    def audio_status(self) -> dict:
        return self.request({"action": "audio_status"})["data"]

    def lip_sync_enable(self, enabled: bool = True) -> None:
        self.request({"action": "lip_sync_enable", "enabled": bool(enabled)})

    def auto_blink_enable(self, enabled: bool = True) -> dict:
        return self.request({"action": "auto_blink_enable", "enabled": bool(enabled)})

    def load_model(self, path: str) -> None:
        self.request({"action": "load", "path": str(path)}, timeout_s=30.0)

    def load_animation(self, path: str) -> None:
        self.request({"action": "load_animation", "path": str(path)}, timeout_s=30.0)

    def play(self) -> None:
        self.request({"action": "play"})

    def blend_to(self, index: int, duration: float | str = 0.15, loop: bool = True) -> None:
        self.request({"action": "blend_to", "index": index, "duration": duration, "loop": loop})
