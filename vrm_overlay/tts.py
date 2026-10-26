"""Local speech providers. No ML Python packages are needed at runtime."""
from __future__ import annotations

from collections import deque
from dataclasses import asdict, dataclass, fields
import json
import os
from pathlib import Path
import re
import secrets
import signal
import socket
import subprocess
import threading
import time
from urllib.error import HTTPError, URLError
from urllib.request import ProxyHandler, Request, build_opener

PROVIDERS = [{"id": "crispasr", "name": "CrispASR (local)"},
             {"id": "piper", "name": "Piper (local)"}]
CRISP_MODELS = [{"id": "chatterbox-nano", "name": "Chatterbox Nano (default)"},
                {"id": "chatterbox-turbo", "name": "Chatterbox Turbo"},
                {"id": "kokoro", "name": "Kokoro"}]


@dataclass(frozen=True)
class SpeechConfig:
    provider: str = "crispasr"
    model: str = "chatterbox-nano"
    executable: str = ""
    model_path: str = ""
    codec_path: str = ""
    voice: str = ""
    language: str = "en"
    device: str = "vulkan"
    gpu_device: int = 0
    threads: int = 4
    timeout_s: float = 180.0

    @classmethod
    def from_dict(cls, values):
        config = cls(**{f.name: values[f.name] for f in fields(cls) if f.name in values})
        if config.provider not in {p["id"] for p in PROVIDERS}:
            raise ValueError("Unknown speech provider")
        if not isinstance(config.model, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,79}", config.model):
            raise ValueError("Invalid speech model/backend name")
        if config.device not in ("cpu", "vulkan", "auto"):
            raise ValueError("Choose CPU, Vulkan or automatic speech processing")
        if type(config.threads) is not int or not 1 <= config.threads <= 64:
            raise ValueError("Speech threads must be between 1 and 64")
        if type(config.gpu_device) is not int or not 0 <= config.gpu_device <= 32:
            raise ValueError("Invalid speech GPU index")
        if type(config.timeout_s) not in (int, float) or not 1 <= config.timeout_s <= 900:
            raise ValueError("Speech timeout must be between 1 and 900 seconds")
        for key in ("executable", "model_path", "codec_path", "voice", "language"):
            if not isinstance(getattr(config, key), str) or "\0" in getattr(config, key):
                raise ValueError(f"Invalid speech setting: {key}")
        return config


class SpeechPreferences:
    """Remember model paths/voices independently when switching profiles."""
    def __init__(self, values=None):
        values = values if isinstance(values, dict) else {}
        self.profiles = {}
        profiles = values.get("profiles", {})
        for key, profile in (profiles.items() if isinstance(profiles, dict) else []):
            try:
                config = SpeechConfig.from_dict(profile)
                self.profiles[self.key(config)] = asdict(config)
            except (ValueError, TypeError, AttributeError):
                continue
        selected = values.get("selected")
        self.selected = selected if isinstance(selected, str) else "crispasr:chatterbox-nano"
        self.config = SpeechConfig.from_dict(self.profiles.get(self.selected, {}))
        self.selected = self.key(self.config)

    @staticmethod
    def key(config):
        return f"{config.provider}:{config.model}"

    def update(self, **changes):
        config = SpeechConfig.from_dict({**asdict(self.config), **changes})
        self.config = config
        self.selected = self.key(config)
        self.profiles[self.selected] = asdict(config)

    def select(self, provider, model):
        self.profiles[self.key(self.config)] = asdict(self.config)
        defaults = asdict(SpeechConfig(provider=provider, model=model))
        self.config = SpeechConfig.from_dict(self.profiles.get(f"{provider}:{model}", defaults))
        self.selected = self.key(self.config)

    def to_dict(self):
        self.profiles[self.key(self.config)] = asdict(self.config)
        return {"version": 1, "selected": self.selected, "profiles": self.profiles}


def _local_file(value, label):
    path = Path(value).expanduser().resolve()
    if not path.is_file():
        raise RuntimeError(f"{label} not found: {path}")
    return str(path)


def _stop_process(process):
    if process is None or process.poll() is not None:
        return
    _terminate(process)
    try:
        process.wait(timeout=1)
    except subprocess.TimeoutExpired:
        _terminate(process, kill=True)
        process.wait(timeout=1)


def _terminate(process, kill=False):
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL if kill else signal.SIGTERM)
        elif kill:
            process.kill()
        else:
            process.terminate()
    except ProcessLookupError:
        pass


class CrispAsrTts:
    """Owned loopback sidecar; the selected model remains resident across calls.

    In-flight cancellation terminates our sidecar, since disconnecting HTTP alone
    does not guarantee native inference stops. The next call starts it again.
    """
    def __init__(self, config: SpeechConfig, on_status=lambda _value: None):
        self.config = SpeechConfig.from_dict(asdict(config))
        self._on_status = on_status
        self._process = None
        self._log = deque(maxlen=40)
        self._log_thread = None
        self._port = 0
        self._key = secrets.token_hex(24)
        self._http = build_opener(ProxyHandler({}))

    def _command(self, port, prepared=None):
        from .tts_install import find_crispasr, model_directory
        config = self.config
        binary = find_crispasr(config.executable, config.device)
        model_path = config.model_path or (str(prepared[0]) if prepared else "")
        model = _local_file(model_path, "Speech model") if model_path else "auto"
        cmd = [binary, "--server", "--host", "127.0.0.1", "--port", str(port),
               "--backend", config.model, "-m", model, "--threads", str(config.threads),
               "-l", config.language or "en", "--no-punctuation", "--cache-dir", str(model_directory())]
        if model == "auto":
            cmd.append("--auto-download")
        codec = config.codec_path or (str(prepared[1]) if prepared and len(prepared) > 1 else "")
        if codec:
            cmd.extend(["--codec-model", _local_file(codec, "Speech decoder")])
        if config.device == "cpu":
            cmd.append("-ng")
        elif config.device == "vulkan":
            cmd.extend(["--gpu-backend", "vulkan", "-dev", str(config.gpu_device)])
        return cmd

    def _request(self, route, payload=None, timeout=0.3):
        data = json.dumps(payload).encode() if payload is not None else None
        request = Request(f"http://127.0.0.1:{self._port}{route}", data=data,
                          headers={"Authorization": f"Bearer {self._key}", "Content-Type": "application/json"})
        return self._http.open(request, timeout=timeout)

    def prepare(self, stop_event=None):
        if self._process is not None and self._process.poll() is None:
            return
        self.close()
        if stop_event is not None and stop_event.is_set():
            raise RuntimeError("Speech synthesis cancelled")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            self._port = probe.getsockname()[1]
        command = self._command(self._port)
        # Check executable/local configuration before starting a large download.
        # Other backends retain native resolution; explicit paths stay user-owned.
        if not self.config.model_path:
            from .tts_models import prepare_model_files
            prepared = prepare_model_files(self.config.model, stop_event, self._on_status,
                                            include_decoder=not self.config.codec_path)
            if prepared:
                # Explicit paths keep native auto-resolution from replacing a
                # verified pinned file with a newer registry revision.
                command = self._command(self._port, prepared)
        self._log.clear()
        self._on_status("Loading speech model… First use may download model files.")
        env = dict(os.environ, CRISPASR_API_KEYS=self._key)
        self._process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                                         start_new_session=(os.name == "posix"))
        pipe = self._process.stdout
        def drain_log():
            while chunk := pipe.read1(4096):
                self._log.append(chunk)
        self._log_thread = threading.Thread(target=drain_log, daemon=True)
        self._log_thread.start()
        deadline = time.monotonic() + 900  # includes first-use model download
        try:
            while True:
                if stop_event is not None and stop_event.is_set():
                    raise RuntimeError("Speech synthesis cancelled")
                if self._process.poll() is not None:
                    self._log_thread.join(timeout=0.2)
                    raise RuntimeError(f"CrispASR exited ({self._process.returncode}): {self.diagnostics()}")
                if time.monotonic() > deadline:
                    raise TimeoutError("CrispASR did not become ready within 15 minutes")
                try:
                    with self._request("/health") as response:
                        health = json.load(response)
                    if health.get("status") == "ok":
                        return
                except HTTPError as error:
                    error.close()
                except (URLError, TimeoutError, OSError, ValueError):
                    pass
                if stop_event is not None:
                    stop_event.wait(0.05)
                else:
                    time.sleep(0.05)
        except BaseException:
            self.close()
            raise

    def diagnostics(self):
        return b"".join(list(self._log))[-3000:].decode(errors="replace").strip() or "No native diagnostics"

    def synth_wav(self, text, out_path, timeout_s=None, stop_event=None):
        self.prepare(stop_event)
        finished = threading.Event()
        process = self._process
        def watch_cancel():
            while not finished.wait(0.03):
                if stop_event is not None and stop_event.is_set():
                    if process.poll() is None:
                        _terminate(process)
                    return
        watcher = threading.Thread(target=watch_cancel, daemon=True)
        watcher.start()
        payload = {"input": text, "response_format": "wav"}
        if self.config.voice:
            payload["voice"] = str(Path(self.config.voice).expanduser()) if "/" in self.config.voice else self.config.voice
        try:
            self._on_status("Synthesizing…")
            with self._request("/v1/audio/speech", payload, timeout_s or self.config.timeout_s) as response:
                audio = response.read(32 * 1024 * 1024 + 1)
            if len(audio) > 32 * 1024 * 1024:
                raise RuntimeError("Speech response exceeded the 32 MB limit")
            if stop_event is not None and stop_event.is_set():
                raise RuntimeError("Speech synthesis cancelled")
            if audio[:4] != b"RIFF" or audio[8:12] != b"WAVE":
                raise RuntimeError("Speech engine returned an invalid WAV")
            out_path = Path(out_path)
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_bytes(audio)
        except HTTPError as error:
            try:
                message = error.read(3000).decode(errors="replace")
            finally:
                error.close()
            raise RuntimeError(f"CrispASR speech request failed ({error.code}): {message}") from error
        except Exception as error:
            self.close()
            if stop_event is not None and stop_event.is_set():
                raise RuntimeError("Speech synthesis cancelled") from error
            raise RuntimeError(f"CrispASR speech request failed: {error}") from error
        finally:
            finished.set()
            watcher.join(timeout=1)
            if stop_event is not None and stop_event.is_set():
                self.close()

    def close(self):
        _stop_process(self._process)
        if self._log_thread is not None:
            self._log_thread.join(timeout=1)
            self._log_thread = None
        if self._process is not None and self._process.stdout is not None:
            self._process.stdout.close()
        self._process = None


def create_tts(config: SpeechConfig, on_status=lambda _value: None):
    config = SpeechConfig.from_dict(asdict(config))
    if config.provider == "crispasr":
        return CrispAsrTts(config, on_status)
    if config.provider == "piper":
        from .speech import PiperTts
        return PiperTts(config.executable or "piper", config.model_path)
    raise ValueError(f"Unknown speech provider: {config.provider}")
