"""Speech settings/synthesis and bounded playback on Studio's single IPC transport."""
from __future__ import annotations

import base64
from dataclasses import asdict
import json
from pathlib import Path
import queue
import tempfile
import threading

from PySide6.QtCore import QObject, Property, QTimer, QUrl, Signal, Slot

from .speech import wav_path_to_pcm24k_mono_s16le
from .tts import CRISP_MODELS, PROVIDERS, SpeechPreferences, create_tts
from .tts_install import find_crispasr, install_crispasr


class SpeechController(QObject):
    changed = Signal()
    _result = Signal(int, object, str)
    _progress = Signal(int, str)

    def __init__(self, settings, submit, connected, parent=None):
        super().__init__(parent)
        self._settings = settings
        try:
            values = json.loads(str(settings.value("speech/profiles", "{}")))
        except (TypeError, ValueError):
            values = {}
        self._preferences = SpeechPreferences(values)
        self._submit = submit
        self._connected = connected
        self._status = "Ready"
        self._error = ""
        self._busy = False
        self._generation = 0
        self._cancelled = threading.Event()
        self._jobs = queue.Queue(maxsize=1)
        self._thread = None
        self._closing = False
        self._pcm = b""
        self._offset = 0
        self._ended = False
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._poll_audio)
        self._result.connect(self._synthesized)
        self._progress.connect(self._set_progress)

    config = Property("QVariantMap", lambda self: asdict(self._preferences.config), notify=changed)
    providers = Property("QVariantList", lambda self: PROVIDERS, constant=True)
    models = Property("QVariantList", lambda self: CRISP_MODELS, constant=True)
    status = Property(str, lambda self: self._status, notify=changed)
    error = Property(str, lambda self: self._error, notify=changed)
    busy = Property(bool, lambda self: self._busy, notify=changed)

    @property
    def generation(self):
        """Session identity for coordinators; changes on cancellation/profile edits."""
        return self._generation

    def _persist(self):
        self._error = ""
        self._settings.setValue("speech/profiles", json.dumps(self._preferences.to_dict()))
        self._settings.sync()
        self.changed.emit()

    @Slot(str)
    def selectProvider(self, provider):
        try:
            self.stop(send_cancel=self._busy)
            model = "piper" if provider == "piper" else str(self._settings.value("speech/last_crisp_model", "chatterbox-nano"))
            self._preferences.select(provider, model)
            self._persist()
            self._release()
        except (ValueError, TypeError) as error:
            self._failed(str(error))

    @Slot(str)
    def selectModel(self, model):
        try:
            self.stop(send_cancel=self._busy)
            self._preferences.select("crispasr", model)
            self._settings.setValue("speech/last_crisp_model", model)
            self._persist()
            self._release()
        except (ValueError, TypeError) as error:
            self._failed(str(error))

    @Slot(str, "QVariant")
    def setOption(self, name, value):
        try:
            if name not in asdict(self._preferences.config) or name in ("provider", "model"):
                raise ValueError("Unknown speech setting")
            if name in ("threads", "gpu_device"):
                value = int(value)
            if isinstance(value, str) and QUrl(value).isLocalFile():
                value = QUrl(value).toLocalFile()
            if self.config[name] == value:
                return
            self._preferences.update(**{name: value})
            self.stop(send_cancel=self._busy)
            self._persist()
            self._release()
        except (ValueError, TypeError) as error:
            self._failed(str(error))

    def _put(self, job):
        while True:
            try:
                self._jobs.get_nowait()
            except queue.Empty:
                break
        self._jobs.put_nowait(job)
        if self._thread is None:
            self._thread = threading.Thread(target=self._work, name="studio-speech", daemon=True)
            self._thread.start()

    def _release(self):
        if self._thread is not None:
            self._put(("release", self._generation, self._preferences.config, "", self._cancelled))

    def _start(self, kind, text=""):
        if self._closing:
            return
        self.stop()
        self._error = ""
        self._cancelled = threading.Event()
        self._busy = True
        self._status = "Preparing speech…" if kind == "prepare" else "Synthesizing…"
        self.changed.emit()
        self._put((kind, self._generation, self._preferences.config, text, self._cancelled))

    @Slot()
    def prepare(self):
        if self._preferences.config.provider == "crispasr":
            self._start("prepare")

    @Slot(str)
    def speak(self, text):
        text = text.strip()
        if not text:
            return
        if not self._connected():
            self._failed("Connect to the renderer before speaking.")
            return
        if len(text) > 4096:
            self._failed("Speak up to 4096 characters at a time.")
            return
        self._start("speak", text)

    @Slot()
    def stop(self, send_cancel=True):
        self._cancelled.set()
        self._generation += 1
        self._timer.stop()
        self._pcm = b""
        self._offset = 0
        was_busy = self._busy
        self._busy = False
        self._status = "Stopped" if was_busy else "Ready"
        if send_cancel and self._connected():
            self._submit([{"action": "audio_cancel"}], "Stop speech", priority=True)
        self.changed.emit()

    def _work(self):
        provider = None
        provider_config = None
        token = 0
        try:
            while True:
                kind, token, config, text, cancelled = self._jobs.get()
                if kind == "shutdown":
                    return
                if kind == "release":
                    if provider and hasattr(provider, "close"):
                        provider.close()
                    provider = provider_config = None
                    continue
                if cancelled.is_set():
                    continue
                try:
                    if kind == "prepare":
                        try:
                            find_crispasr(config.executable, config.device)
                        except RuntimeError:
                            if config.executable:
                                raise
                            install_crispasr(config.device, stop_event=cancelled,
                                            on_status=lambda value: self._progress.emit(token, value))
                    if provider_config != config:
                        if provider and hasattr(provider, "close"):
                            provider.close()
                        provider = create_tts(config, lambda value: self._progress.emit(token, value))
                        provider_config = config
                    if kind == "prepare":
                        provider.prepare(cancelled)
                        self._result.emit(token, None, "")
                    else:
                        with tempfile.TemporaryDirectory(prefix="vrm-studio-speech-") as directory:
                            wav = Path(directory) / "speech.wav"
                            provider.synth_wav(text, wav, stop_event=cancelled)
                            if not cancelled.is_set():
                                self._result.emit(token, wav_path_to_pcm24k_mono_s16le(wav), "")
                except Exception as error:
                    if not cancelled.is_set():
                        self._result.emit(token, None, str(error))
        finally:
            if provider and hasattr(provider, "close"):
                provider.close()

    @Slot(int, str)
    def _set_progress(self, token, message):
        if token == self._generation and self._busy:
            self._status = message
            self.changed.emit()

    @Slot(int, object, str)
    def _synthesized(self, token, pcm, error):
        if token != self._generation or not self._busy:
            return
        if error:
            self._failed(error)
        elif pcm is None:
            self._busy = False
            self._status = "Speech model ready"
            self.changed.emit()
        elif not pcm:
            self._failed("Speech engine produced no audio.")
        elif not self._connected():
            self._failed("Renderer disconnected before speech could play.")
        else:
            self._pcm = pcm
            self._offset = 0
            self._ended = False
            self._request("audio_stream_begin", {}, lambda _reply: self._timer.start(0))

    def _request(self, action, values, done):
        token = self._generation
        def current():
            return token == self._generation and self._busy and not self._closing
        self._submit([dict(values, action=action)], "Speech playback",
                     done=lambda response: done(response) if current() else None,
                     failed=lambda message: self._failed(message) if current() else None,
                     guard=current)

    def _poll_audio(self):
        if self._busy:
            self._request("audio_status", {}, self._audio_status)

    def _audio_status(self, response):
        state = response.get("data", {})
        if not state.get("active"):
            self._failed("Audio playback device is unavailable.")
            return
        if self._offset < len(self._pcm):
            if float(state.get("available_seconds", 0)) < 0.5:
                chunk = self._pcm[self._offset:self._offset + 4800]
                self._offset += len(chunk)
                self._status = "Speaking"
                self.changed.emit()
                self._request("audio_stream_chunk", {"data": base64.b64encode(chunk).decode()},
                              lambda _reply: self._timer.start(0))
            else:
                self._timer.start(30)
        elif not self._ended:
            self._ended = True
            self._request("audio_stream_end", {}, lambda _reply: self._timer.start(30))
        elif state.get("drained"):
            self._pcm = b""
            self._busy = False
            self._status = "Ready"
            self.changed.emit()
        else:
            self._timer.start(30)

    def _failed(self, message):
        self.stop()
        self._error = message
        self._status = "Speech unavailable"
        self.changed.emit()

    def shutdown(self):
        if self._closing:
            return
        self.stop(send_cancel=False)
        self._closing = True
        if self._thread is not None:
            self._put(("shutdown", self._generation, self._preferences.config, "", self._cancelled))
            self._thread.join(timeout=2)
