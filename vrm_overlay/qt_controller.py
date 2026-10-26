"""Qt event-loop controller. All socket/process work is asynchronous."""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
import json
from pathlib import Path
import time
from typing import Callable

from PySide6.QtCore import QObject, Property, QProcess, QSettings, QTimer, QUrl, Signal, Slot
from PySide6.QtNetwork import QLocalSocket
from .qt_speech import SpeechController

ROOT = Path(__file__).resolve().parent.parent


def local_path(value: str) -> Path:
    url = QUrl(value)
    if url.isLocalFile():
        return Path(url.toLocalFile()).resolve()
    return Path(value).expanduser().resolve()


def initial_state() -> dict:
    return dict(model_loaded=False, model_path="", animation_path="", playing=False,
                animation_index=-1, time=0.0, duration=0.0, speed=1.0, loop=False,
                backend="legacy", ozz_available=False, frame_rate=0.0, visible=True,
                always_on_top=False, auto_resize=True, scale=1.0, shader="anime",
                lip_sync=True, correction=0, expressions=[], clips=[],
                spring=dict(stiffness=1.0, drag=1.0, gravity=1.0, cloth_mode=False,
                            cloth_stiffness=0.35, cloth_drag=1.2, cloth_gravity=1.0, cloth_taper=0.6),
                blend=dict(duration=0.45, seam=0.15, groups=[1.0]*9, active=False, progress=0),
                physics=dict(garment_collisions=True, garment_chains=0, arm_colliders=0, generated_arm_colliders=0, contacts=0, unresolved_contacts=0, max_penetration=0, garment_arm_penetration=0, steps=0, dropped_time=0),
                foot_lock=dict(enabled=True, supported=0, pelvis_offset=0,
                               left=dict(supported=False, planted=False, weight=0, error=0),
                               right=dict(supported=False, planted=False, weight=0, error=0)),
                blink=dict(enabled=False, rate=14.0, duration=150.0),
                camera=dict(azimuth=3.14, polar=1.57, dist=3.0))


@dataclass
class Job:
    label: str
    commands: deque
    done: Callable | None = None
    poll: bool = False
    failed: Callable | None = None
    guard: Callable | None = None


class Controller(QObject):
    changed = Signal()
    stateChanged = Signal()
    libraryChanged = Signal()
    logChanged = Signal()
    operationFinished = Signal(str)

    def __init__(self, socket_path="/tmp/vrm-overlay.sock", *, attach=False,
                 model=None, animation_dir=None, settings=None, parent=None):
        super().__init__(parent)
        self._settings = settings if settings is not None else QSettings()
        self._socket_path = socket_path
        self._attach = attach
        self._state = initial_state()
        self._connected = False
        self._status = "Disconnected"
        self._error = ""
        self._logs = deque(maxlen=200)
        self._model = str(local_path(model or self._settings.value("model", str(ROOT / "test.vrm"))))
        self._folder = str(local_path(animation_dir or self._settings.value("animations", str(ROOT / "animation"))))
        self._audio = str(self._settings.value("audio", str(ROOT / "test.wav")))
        self._files = []
        self._selected = -1
        self._jobs = deque()
        self._job = None
        self._pending = None
        self._next_id = 1
        self._buffer = bytearray()
        self._starting = False
        self._owns_process = False
        self._initialised = False
        self._closing = False
        self._deadline = 0
        self._tuning = {}
        self._socket = QLocalSocket(self)
        self._socket.setReadBufferSize(1024 * 1024)
        self._socket.connected.connect(self._transport_connected)
        self._socket.disconnected.connect(self._disconnected)
        self._socket.readyRead.connect(self._read)
        self._socket.errorOccurred.connect(self._socket_error)
        self._process = QProcess(self)
        self._process.setProcessChannelMode(QProcess.MergedChannels)
        self._process.readyReadStandardOutput.connect(self._read_process_log)
        self._process.finished.connect(self._process_finished)
        self._process.errorOccurred.connect(self._process_error)
        self._timeout = QTimer(self)
        self._timeout.setSingleShot(True)
        self._timeout.timeout.connect(self._timed_out)
        self._retry = QTimer(self)
        self._retry.setInterval(200)
        self._retry.timeout.connect(self._attempt_connect)
        self._poll = QTimer(self)
        self._poll.setInterval(250)
        self._poll.timeout.connect(self.refresh)
        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(100)
        self._debounce.timeout.connect(self._flush_tuning)
        self._speech = SpeechController(self._settings, self._queue, lambda: self._connected, self)
        self.scanAnimations()

    connected = Property(bool, lambda self: self._connected, notify=changed)
    busy = Property(bool, lambda self: bool(self._jobs) or (self._job is not None and not self._job.poll), notify=changed)
    status = Property(str, lambda self: self._status, notify=changed)
    lastError = Property(str, lambda self: self._error, notify=changed)
    socketPath = Property(str, lambda self: self._socket_path, notify=changed)
    modelPath = Property(str, lambda self: self._model, notify=changed)
    audioPath = Property(str, lambda self: self._audio, notify=changed)
    animationFolder = Property(str, lambda self: self._folder, notify=libraryChanged)
    animationFiles = Property("QVariantList", lambda self: self._files, notify=libraryChanged)
    selectedAnimation = Property(int, lambda self: self._selected, notify=libraryChanged)
    state = Property("QVariantMap", lambda self: self._state, notify=stateChanged)
    logText = Property(str, lambda self: "\n".join(self._logs), notify=logChanged)
    ownsServer = Property(bool, lambda self: self._owns_process, notify=changed)
    speech = Property(QObject, lambda self: self._speech, constant=True)

    def _log(self, message):
        self._logs.append(message)
        self.logChanged.emit()

    def _fail(self, message):
        self._error = message
        self._log(message)
        self.changed.emit()

    @Slot()
    def clearError(self):
        self._error = ""
        self.changed.emit()

    @Slot()
    def connectRuntime(self):
        self.disconnectRuntime()
        self._closing = False
        self._starting = True
        self._initialised = False
        self._deadline = time.monotonic() + 15
        self._status = "Connecting…"
        self._error = ""
        self.changed.emit()
        self._retry.start()
        self._attempt_connect()

    def _attempt_connect(self):
        if time.monotonic() > self._deadline:
            self._retry.stop()
            self._starting = False
            self._fail("Runtime did not become ready. See the log, or start vrm-overlay manually.")
            self._status = "Disconnected"
            self.changed.emit()
            return
        if self._socket.state() == QLocalSocket.UnconnectedState:
            self._socket.connectToServer(self._socket_path)

    def _socket_error(self, _error):
        if self._starting and self._socket.error() in (
            QLocalSocket.ServerNotFoundError, QLocalSocket.ConnectionRefusedError
        ):
            if self._attach:
                self._starting = False
                self._retry.stop()
                self._fail(f"No runtime at {self._socket_path}. Start it first or launch without --attach.")
                self._status = "Disconnected"
                self.changed.emit()
            elif self._process.state() == QProcess.NotRunning and not self._owns_process:
                binary = ROOT / "vrm-overlay"
                if not binary.is_file():
                    self._retry.stop()
                    self._starting = False
                    self._fail("Build the renderer with make before starting the controller.")
                    return
                self._owns_process = True
                self._process.setWorkingDirectory(str(ROOT))
                self._process.start(str(binary), ["--socket", self._socket_path])
                self._status = "Starting renderer…"
                self.changed.emit()
            return
        if not self._closing:
            self._fail(self._socket.errorString())

    def _transport_connected(self):
        self._retry.stop()
        self._starting = False
        self._buffer.clear()
        self._status = "Checking runtime…"
        self.changed.emit()
        self._queue([{"action": "get_state"}], "Connect", self._became_ready, poll=True)

    def _became_ready(self, response):
        if not self._update_state(response):
            self.disconnectRuntime()
            return
        self._connected = True
        self._status = "Connected"
        self._poll.start()
        self.changed.emit()
        if self._owns_process and not self._initialised:
            self._initialised = True
            if not self._state["model_loaded"] and Path(self._model).is_file():
                commands = [{"action": "load", "path": self._model}]
                if self._state["ozz_available"]:
                    commands.append({"action": "set_anim_backend", "backend": "ozz"})
                relaxed = next((f for f in self._files if f["name"].lower() == "relax"), None)
                if relaxed:
                    commands.extend(self._animation_commands(relaxed["path"], loop=True))
                commands.append({"action": "auto_blink_enable", "enabled": True})
                self._queue(commands, "Load avatar")

    @Slot()
    def disconnectRuntime(self):
        if self._speech.busy and self._socket.state() == QLocalSocket.ConnectedState:
            self._socket.write(b'{"action":"audio_cancel"}\n')
            self._socket.flush()
        self._speech.stop(send_cancel=False)
        self._retry.stop()
        self._poll.stop()
        self._timeout.stop()
        self._debounce.stop()
        self._tuning.clear()
        self._jobs.clear()
        self._job = self._pending = None
        self._starting = False
        self._connected = False
        self._socket.abort()
        self._status = "Disconnected"
        self.changed.emit()

    def _disconnected(self):
        was_connected = self._connected
        self.disconnectRuntime()
        if was_connected and not self._closing:
            self._fail("Runtime disconnected. Reconnect to continue.")

    def _process_finished(self, code, _status):
        self._owns_process = False
        if not self._closing:
            self.disconnectRuntime()
            self._fail(f"Renderer exited (status {code}). See the log.")
        self.changed.emit()

    def _process_error(self, _error):
        if not self._closing:
            self._owns_process = False
            self.disconnectRuntime()
            self._fail(self._process.errorString())

    def _read_process_log(self):
        output = bytes(self._process.readAllStandardOutput()).decode(errors="replace")
        for line in output.splitlines():
            self._log(line)

    def _queue(self, commands, label, done=None, poll=False, failed=None, guard=None, priority=False):
        if len(self._jobs) >= 32:
            message = "Too many queued actions; wait for the current operation."
            failed(message) if failed else self._fail(message)
            return
        job = Job(label, deque(commands), done, poll, failed, guard)
        if priority:
            self._jobs.appendleft(job)
        else:
            self._jobs.append(job)
        self._advance()

    def _advance(self):
        if self._pending is not None:
            return
        if self._job is None:
            if not self._jobs:
                self.changed.emit()
                return
            self._job = self._jobs.popleft()
        if self._job.guard is not None and not self._job.guard():
            self._job = None
            self._advance()
            return
        if not self._job.commands:
            job = self._job
            self._job = None
            if not job.poll:
                self._status = "Connected"
                self._error = ""
                self.operationFinished.emit(job.label)
                self.refresh()
            self._advance()
            return
        payload = dict(self._job.commands.popleft())
        request_id = self._next_id
        self._next_id += 1
        payload["id"] = request_id
        self._pending = request_id
        if not self._job.poll:
            self._status = self._job.label + "…"
        data = (json.dumps(payload, allow_nan=False) + "\n").encode()
        if self._socket.write(data) != len(data):
            self._timed_out()
            return
        self._timeout.start(30000 if payload["action"] in ("load", "load_animation") else 3000)
        self.changed.emit()

    def _read(self):
        self._buffer.extend(bytes(self._socket.readAll()))
        if len(self._buffer) > 1024 * 1024:
            self._fail("Runtime response exceeded the size limit.")
            self.disconnectRuntime()
            return
        while b"\n" in self._buffer:
            line, _, rest = self._buffer.partition(b"\n")
            self._buffer = bytearray(rest)
            try:
                response = json.loads(line)
                if not isinstance(response, dict):
                    raise ValueError("Response is not an object")
            except (ValueError, UnicodeError):
                self._fail("Invalid response from runtime.")
                self.disconnectRuntime()
                return
            if response.get("id") != self._pending:
                continue
            self._timeout.stop()
            self._pending = None
            if response.get("status") != "ok":
                label = self._job.label
                failed = self._job.failed
                self._job = None  # Abort the rest of this transaction.
                message = f"{label}: {(response.get('error') or {}).get('message', 'Command failed')}"
                failed(message) if failed else self._fail(message)
                self._status = "Connected" if self._connected else "Disconnected"
                self._advance()
                continue
            if not self._job.commands and self._job.done:
                self._job.done(response)
            self._advance()

    def _timed_out(self):
        label = self._job.label if self._job else "Request"
        self.disconnectRuntime()
        self._fail(f"{label} timed out. The command may have run. Another controller may own this socket.")

    def _update_state(self, response):
        state = response.get("data", {})
        if not isinstance(state, dict) or state.get("api_version") != 1:
            self._fail("This runtime does not support the Qt controller. Rebuild with make.")
            return False
        self._state = state
        selected = next((i for i, f in enumerate(self._files) if f["path"] == state["animation_path"]), -1)
        if selected != self._selected:
            self._selected = selected
            self.libraryChanged.emit()
        self.stateChanged.emit()
        return True

    @Slot()
    def refresh(self):
        if self._connected and not self._jobs and self._job is None:
            self._queue([{"action": "get_state"}], "Refresh", self._update_state, poll=True)

    @Slot(str, "QVariantMap")
    def command(self, action, values):
        if not self._connected:
            self._fail("Connect to the renderer first.")
            return
        if self._tuning:
            self._flush_tuning()
        self._queue([dict(values, action=action)], action.replace("_", " ").capitalize())

    @Slot(str, str, float)
    def tune(self, action, key, value):
        if self._connected:
            self._tuning.setdefault(action, {})[key] = value
            self._debounce.start()

    def _flush_tuning(self):
        self._debounce.stop()
        changes, self._tuning = self._tuning, {}
        for action, values in changes.items():
            self.command(action, values)

    @Slot(str)
    def loadModel(self, path):
        model = local_path(path)
        if not model.is_file():
            self._fail(f"Model file does not exist: {model}")
            return
        self._model = str(model)
        self._settings.setValue("model", self._model)
        self.changed.emit()
        self.command("load", {"path": self._model})

    @Slot()
    def scanAnimations(self):
        folder = Path(self._folder)
        try:
            self._files = [dict(name=p.stem, file=p.name, path=str(p.resolve()))
                           for p in sorted(folder.iterdir(), key=lambda p: p.name.casefold())
                           if p.is_file() and p.suffix.lower() in (".vrma", ".glb")] if folder.is_dir() else []
        except OSError as error:
            self._files = []
            self._fail(f"Cannot read animation folder: {error}")
        self._selected = next((i for i, f in enumerate(self._files) if f["path"] == self._state["animation_path"]), -1)
        self.libraryChanged.emit()

    @Slot(str)
    def setAnimationFolder(self, path):
        folder = local_path(path)
        if not folder.is_dir():
            self._fail(f"Animation folder does not exist: {folder}")
            return
        self._folder = str(folder)
        self._settings.setValue("animations", self._folder)
        self.scanAnimations()

    def _animation_commands(self, path, loop=None):
        return [{"action": "load_animation", "path": path},
                {"action": "blend_to", "index": 0, "duration": self._state["blend"]["duration"],
                 "loop": self._state["loop"] if loop is None else loop},
                {"action": "play", "speed": self._state["speed"]}]

    @Slot(int)
    def playAnimation(self, index):
        if not self._connected or not self._state["model_loaded"] or not 0 <= index < len(self._files):
            return
        self._flush_tuning()
        self._queue(self._animation_commands(self._files[index]["path"]), "Play " + self._files[index]["name"])

    @Slot(int)
    def stepAnimation(self, delta):
        if self._files:
            index = (self._selected + delta) % len(self._files) if self._selected >= 0 else 0
            self.playAnimation(index)

    @Slot(str)
    def playAudio(self, path):
        audio = local_path(path)
        if not audio.is_file():
            self._fail(f"Audio file does not exist: {audio}")
            return
        self._audio = str(audio)
        self._speech.stop()
        self._settings.setValue("audio", self._audio)
        self.changed.emit()
        self._queue([{"action": "play_audio_file", "path": self._audio}], "Play audio")

    @Slot()
    def shutdown(self):
        self._closing = True
        if self._speech.busy and self._socket.state() == QLocalSocket.ConnectedState:
            self._socket.write(b'{"action":"audio_cancel"}\n')
            self._socket.flush()
        self._speech.shutdown()
        self._poll.stop()
        self._retry.stop()
        if self._owns_process and self._process.state() != QProcess.NotRunning:
            if self._socket.state() == QLocalSocket.ConnectedState:
                self._socket.write(b'{"action":"shutdown"}\n')
                self._socket.flush()
            if not self._process.waitForFinished(1500):
                self._process.terminate()
                if not self._process.waitForFinished(1000):
                    self._process.kill()
                    self._process.waitForFinished(1000)
        self.disconnectRuntime()
