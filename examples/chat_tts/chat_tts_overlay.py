from __future__ import annotations

import base64
import json
import logging
import os
import queue
import re
import socket
import subprocess
import sys
import threading
import time
import traceback
import wave
from dataclasses import asdict, dataclass
from dataclasses import fields as dataclass_fields
from pathlib import Path
from typing import Callable, Iterable, Optional

import requests
from requests import Response
import tkinter as tk
from tkinter import messagebox, ttk


DEFAULT_ENDPOINT = "https://api.openai.com/v1/chat/completions"
DEFAULT_MODEL = "gpt-4o-mini"
DEFAULT_SOCKET_PATH = "/tmp/vrm-overlay.sock"
DEFAULT_SYSTEM_PROMPT = "You are a helpful assistant."

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent

DEFAULT_VRM_PATH = str((REPO_ROOT / "test.vrm").resolve())
DEFAULT_ANIM_PATH = str((REPO_ROOT / "animation" / "Relax.vrma").resolve())

THEME_AUTO = "auto"
THEME_LIGHT = "light"
THEME_DARK = "dark"

DEFAULT_CONNECT_TIMEOUT_S = 10.0
DEFAULT_READ_TIMEOUT_S = 60.0


def _config_path() -> Path:
    xdg = os.environ.get("XDG_CONFIG_HOME")
    base = Path(xdg) if xdg else (Path.home() / ".config")
    return base / "vrm-overlay" / "chat_tts_settings.json"


def _log_path() -> Path:
    cfg = _config_path()
    return cfg.parent / "chat_tts.log"


def setup_logger() -> logging.Logger:
    log = logging.getLogger("chat_tts_overlay")
    if log.handlers:
        return log

    log.setLevel(logging.DEBUG)
    log_path = _log_path()
    log_path.parent.mkdir(parents=True, exist_ok=True)

    fh = logging.FileHandler(log_path, encoding="utf-8")
    fh.setLevel(logging.DEBUG)
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s")
    fh.setFormatter(fmt)
    log.addHandler(fh)

    sh = logging.StreamHandler(sys.stderr)
    sh.setLevel(logging.INFO)
    sh.setFormatter(fmt)
    log.addHandler(sh)

    log.debug("Logger initialized; log_path=%s", str(log_path))
    return log


@dataclass
class Settings:
    endpoint: str = DEFAULT_ENDPOINT
    api_key: str = ""
    model: str = DEFAULT_MODEL
    system_prompt: str = DEFAULT_SYSTEM_PROMPT
    overlay_socket_path: str = DEFAULT_SOCKET_PATH
    vrm_path: str = DEFAULT_VRM_PATH
    animation_path: str = DEFAULT_ANIM_PATH
    theme: str = THEME_AUTO
    connect_timeout_s: float = DEFAULT_CONNECT_TIMEOUT_S
    read_timeout_s: float = DEFAULT_READ_TIMEOUT_S
    auto_blink: bool = True
    piper_bin: str = os.environ.get("PIPER_BIN", "piper")
    piper_model: str = os.environ.get("PIPER_MODEL", "")

    @classmethod
    def load(cls, path: Path) -> "Settings":
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            allowed = {f.name for f in dataclass_fields(cls)}
            merged = {**asdict(cls()), **{k: v for (k, v) in raw.items() if k in allowed}}
            return cls(**merged)
        except FileNotFoundError:
            return cls()
        except Exception:
            return cls()

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(asdict(self), indent=2, sort_keys=True) + "\n", encoding="utf-8")


class IpcClient:
    def __init__(self, socket_path: str):
        self.socket_path = socket_path
        self._lock = threading.Lock()
        self._sock: Optional[socket.socket] = None

    def connect(self) -> None:
        with self._lock:
            if self._sock is not None:
                return
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(self.socket_path)
            self._sock = sock

    def close(self) -> None:
        with self._lock:
            if self._sock is None:
                return
            try:
                self._sock.close()
            finally:
                self._sock = None

    def request(self, payload: dict, timeout_s: float = 1.0) -> Optional[dict]:
        data = (json.dumps(payload) + "\n").encode("utf-8")
        with self._lock:
            if self._sock is None:
                self.connect()
            assert self._sock is not None
            prev_timeout = self._sock.gettimeout()
            try:
                # Don't let a short recv timeout affect sendall().
                self._sock.settimeout(None)
                self._sock.sendall(data)
                self._sock.settimeout(timeout_s)
            finally:
                # recv loop below uses the per-request timeout; restore at the end.
                pass
            buf = bytearray()
            try:
                while b"\n" not in buf:
                    chunk = self._sock.recv(65536)
                    if not chunk:
                        break
                    buf.extend(chunk)
            except socket.timeout:
                return None
            finally:
                try:
                    self._sock.settimeout(prev_timeout)
                except Exception:
                    pass
        if not buf:
            return None
        try:
            line = bytes(buf).split(b"\n", 1)[0]
            return json.loads(line.decode("utf-8"))
        except Exception:
            return None

    def send_only(self, payload: dict, timeout_s: float = 5.0) -> None:
        data = (json.dumps(payload) + "\n").encode("utf-8")
        with self._lock:
            if self._sock is None:
                self.connect()
            assert self._sock is not None
            prev_timeout = self._sock.gettimeout()
            try:
                self._sock.settimeout(timeout_s)
                self._sock.sendall(data)
            except (BrokenPipeError, TimeoutError, OSError):
                try:
                    self._sock.close()
                except Exception:
                    pass
                self._sock = None
                self.connect()
                assert self._sock is not None
                self._sock.settimeout(timeout_s)
                self._sock.sendall(data)
            finally:
                try:
                    self._sock.settimeout(prev_timeout)
                except Exception:
                    pass

    def audio_stream_begin(self) -> None:
        self.send_only({"action": "audio_stream_begin"}, timeout_s=2.0)

    def audio_stream_chunk(self, pcm_bytes: bytes) -> None:
        b64 = base64.b64encode(pcm_bytes).decode("ascii")
        self.send_only({"action": "audio_stream_chunk", "data": b64}, timeout_s=5.0)

    def audio_stream_end(self) -> None:
        self.send_only({"action": "audio_stream_end"}, timeout_s=2.0)

    def lip_sync_enable(self, enabled: bool = True) -> None:
        self.send_only({"action": "lip_sync_enable", "enabled": bool(enabled)}, timeout_s=2.0)

    def audio_status(self) -> Optional[dict]:
        resp = self.request({"action": "audio_status"}, timeout_s=1.0)
        if not resp:
            return None
        return resp.get("data") or resp

    def auto_blink_enable(self, enabled: bool = True) -> Optional[dict]:
        return self.request({"action": "auto_blink_enable", "enabled": bool(enabled)}, timeout_s=1.0)

    def load_model(self, path: str) -> None:
        self.request({"action": "load", "path": path}, timeout_s=5.0)

    def load_animation(self, path: str) -> None:
        self.request({"action": "load_animation", "path": path}, timeout_s=5.0)

    def play(self) -> None:
        self.request({"action": "play"}, timeout_s=1.0)

    def blend_to(self, index: int, duration: float = 0.15, loop: bool = True) -> None:
        self.request(
            {"action": "blend_to", "index": int(index), "duration": float(duration), "loop": bool(loop)},
            timeout_s=2.0,
        )


class SentenceSegmenter:
    _end_re = re.compile(r"([.!?]+)(\s+|$)")

    def __init__(self) -> None:
        self._buf = ""

    def push(self, text: str) -> list[str]:
        self._buf += text
        out: list[str] = []
        while True:
            m = self._end_re.search(self._buf)
            if not m:
                break
            end_idx = m.end(1)
            sentence = self._buf[:end_idx].strip()
            self._buf = self._buf[end_idx:].lstrip()
            if sentence:
                out.append(sentence)
        return out

    def flush(self) -> Optional[str]:
        rest = self._buf.strip()
        self._buf = ""
        return rest or None


_MD_FENCE_RE = re.compile(r"```.*?```", re.DOTALL)
_MD_INLINE_CODE_RE = re.compile(r"`([^`]+)`")
_MD_LINK_RE = re.compile(r"\[([^\]]+)\]\([^)]+\)")
_MD_IMAGE_RE = re.compile(r"!\[([^\]]*)\]\([^)]+\)")
_MD_HEADING_RE = re.compile(r"^\s{0,3}#{1,6}\s+")
_MD_QUOTE_RE = re.compile(r"^\s{0,3}>\s?")
_MD_LIST_RE = re.compile(r"^\s*([-*+]|\d+\.)\s+")


def clean_for_tts(text: str) -> str:
    s = text or ""
    s = _MD_FENCE_RE.sub(" ", s)
    s = _MD_IMAGE_RE.sub(r"\1", s)
    s = _MD_LINK_RE.sub(r"\1", s)
    s = _MD_INLINE_CODE_RE.sub(r"\1", s)
    s = s.replace("***", " ").replace("**", " ").replace("__", " ").replace("~~", " ")
    s = s.replace("*", " ").replace("_", " ").replace("`", " ")
    lines = []
    for line in s.splitlines():
        line = _MD_HEADING_RE.sub("", line)
        line = _MD_QUOTE_RE.sub("", line)
        line = _MD_LIST_RE.sub("", line)
        lines.append(line)
    s = " ".join(lines)
    s = re.sub(r"\s+", " ", s).strip()
    return s


def detect_system_theme() -> str:
    if sys.platform == "darwin":
        try:
            r = subprocess.run(
                ["defaults", "read", "-g", "AppleInterfaceStyle"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                check=False,
                text=True,
            )
            if r.returncode == 0 and "Dark" in (r.stdout or ""):
                return THEME_DARK
        except Exception:
            pass
        return THEME_LIGHT

    if sys.platform.startswith("win"):
        try:
            import winreg

            key_path = r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as k:
                value, _typ = winreg.QueryValueEx(k, "AppsUseLightTheme")
            return THEME_LIGHT if int(value) == 1 else THEME_DARK
        except Exception:
            return THEME_LIGHT

    # Linux/other: best-effort via gsettings.
    try:
        r = subprocess.run(
            ["gsettings", "get", "org.gnome.desktop.interface", "color-scheme"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            text=True,
        )
        s = (r.stdout or "").strip().strip("'").lower()
        if "dark" in s:
            return THEME_DARK
    except Exception:
        pass
    return THEME_LIGHT


def apply_theme(root: tk.Tk, theme: str) -> dict:
    theme = (theme or THEME_AUTO).strip().lower()
    if theme == THEME_AUTO:
        theme = detect_system_theme()
    if theme not in (THEME_LIGHT, THEME_DARK):
        theme = THEME_LIGHT

    colors = (
        {
            "bg": "#1e1f22",
            "fg": "#e6e6e6",
            "muted": "#a9a9a9",
            "entry_bg": "#2b2d31",
            "button_bg": "#2b2d31",
            "accent": "#4c8bf5",
            "select_bg": "#2f4a7a",
        }
        if theme == THEME_DARK
        else {
            "bg": "#f2f2f2",
            "fg": "#111111",
            "muted": "#444444",
            "entry_bg": "#ffffff",
            "button_bg": "#ffffff",
            "accent": "#2a66d9",
            "select_bg": "#cfe0ff",
        }
    )

    style = ttk.Style(root)
    if "clam" in style.theme_names():
        style.theme_use("clam")

    root.configure(background=colors["bg"])

    style.configure(".", background=colors["bg"], foreground=colors["fg"])
    style.configure("TFrame", background=colors["bg"])
    style.configure("TLabel", background=colors["bg"], foreground=colors["fg"])
    style.configure("TButton", background=colors["button_bg"], foreground=colors["fg"], padding=6)
    style.map(
        "TButton",
        background=[("active", colors["entry_bg"])],
        foreground=[("disabled", colors["muted"])],
    )
    style.configure(
        "TEntry",
        fieldbackground=colors["entry_bg"],
        foreground=colors["fg"],
        insertcolor=colors["fg"],
    )
    style.configure("TSeparator", background=colors["muted"])

    return colors


def wav_path_to_pcm24k_mono_s16le(wav_path: Path) -> bytes:
    with wave.open(str(wav_path), "rb") as wf:
        channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        rate = wf.getframerate()
        frames = wf.readframes(wf.getnframes())

    if channels not in (1, 2):
        raise ValueError(f"Unsupported channel count: {channels}")
    if sampwidth not in (1, 2, 3, 4):
        raise ValueError(f"Unsupported sample width: {sampwidth}")

    # Decode to signed mono int16 samples.
    samples_i16 = _decode_wav_pcm_to_mono_i16(frames, channels=channels, sampwidth=sampwidth)

    # Resample to 24kHz if needed.
    if rate != 24000:
        samples_i16 = _resample_i16_linear(samples_i16, src_rate=rate, dst_rate=24000)

    return samples_i16.tobytes()


def _decode_wav_pcm_to_mono_i16(frames: bytes, channels: int, sampwidth: int) -> "array.array":
    import array

    if sampwidth == 2:
        a = array.array("h")
        a.frombytes(frames)
        if sys.byteorder != "little":
            a.byteswap()
        if channels == 1:
            return a
        out = array.array("h")
        out.extend(((a[i] + a[i + 1]) // 2) for i in range(0, len(a) - 1, 2))
        return out

    if sampwidth == 1:
        # 8-bit PCM is unsigned in WAV.
        raw = memoryview(frames)
        if channels == 1:
            mono = [(b - 128) << 8 for b in raw]
        else:
            mono = [(((raw[i] - 128) + (raw[i + 1] - 128)) // 2) << 8 for i in range(0, len(raw) - 1, 2)]
        out = array.array("h", mono)
        return out

    if sampwidth == 4:
        # Assume signed 32-bit PCM; downscale to int16.
        a32 = array.array("i")
        a32.frombytes(frames)
        if sys.byteorder != "little":
            a32.byteswap()
        if channels == 2:
            a32 = array.array("i", ((a32[i] + a32[i + 1]) // 2 for i in range(0, len(a32) - 1, 2)))
        out = array.array("h", (_clamp_i16(x >> 16) for x in a32))
        return out

    # 24-bit PCM (3 bytes per sample), little-endian.
    b = memoryview(frames)
    step = 3 * channels
    out = array.array("h")

    def read_s24_le(off: int) -> int:
        v = int(b[off]) | (int(b[off + 1]) << 8) | (int(b[off + 2]) << 16)
        if v & 0x800000:
            v -= 0x1000000
        return v

    for off in range(0, len(b) - (step - 1), step):
        if channels == 1:
            s = read_s24_le(off)
        else:
            s = (read_s24_le(off) + read_s24_le(off + 3)) // 2
        out.append(_clamp_i16(s >> 8))
    return out


def _resample_i16_linear(samples: "array.array", src_rate: int, dst_rate: int) -> "array.array":
    import array

    if src_rate <= 0 or dst_rate <= 0:
        return samples
    if src_rate == dst_rate:
        return samples
    if len(samples) < 2:
        return samples

    scale = src_rate / float(dst_rate)
    out_len = max(1, int(len(samples) / scale))
    out = array.array("h")
    out.extend([0] * out_len)

    for i in range(out_len):
        pos = i * scale
        idx = int(pos)
        frac = pos - idx
        if idx >= len(samples) - 1:
            out[i] = samples[-1]
        else:
            a = samples[idx]
            b = samples[idx + 1]
            v = int(round(a * (1.0 - frac) + b * frac))
            out[i] = _clamp_i16(v)
    return out


def _clamp_i16(v: int) -> int:
    if v > 32767:
        return 32767
    if v < -32768:
        return -32768
    return int(v)


class PiperTts:
    def __init__(self, piper_bin: str, model_path: str):
        self.piper_bin = piper_bin
        self.model_path = model_path

    def synth_wav(self, text: str, out_path: Path, timeout_s: float = 60.0) -> None:
        if not self.model_path:
            raise RuntimeError(
                "Missing Piper model path. Set it in Settings (Piper model) or via PIPER_MODEL env var."
            )
        model_path = Path(self.model_path).expanduser()
        if not model_path.exists():
            raise RuntimeError(f"Piper model not found: {str(model_path)}")

        # Many Piper installs require a config file next to the .onnx:
        #   voice.onnx
        #   voice.onnx.json
        cfg_path = Path(str(model_path) + ".json")
        if not cfg_path.exists():
            raise RuntimeError(
                "Piper config missing. Expected a JSON next to the model: "
                f"{str(cfg_path)} (download the matching .onnx.json for your voice)."
            )

        out_path.parent.mkdir(parents=True, exist_ok=True)
        cmd = [self.piper_bin, "--model", str(model_path), "--output_file", str(out_path)]
        subprocess.run(cmd, input=text.encode("utf-8"), check=True, timeout=timeout_s)


def iter_chat_completions_stream(
    endpoint: str,
    api_key: str,
    model: str,
    messages: list[dict],
    connect_timeout_s: float,
    read_timeout_s: float,
    log: logging.Logger,
    stop_event: threading.Event,
    on_response: Callable[[Optional[Response]], None],
) -> Iterable[str]:
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"
    headers["Accept"] = "text/event-stream"

    payload = {"model": model, "messages": messages, "stream": True}

    safe_endpoint = endpoint
    log.info("OpenAI request start endpoint=%s model=%s", safe_endpoint, model)
    r: Optional[Response] = None
    try:
        r = requests.post(
            endpoint,
            headers=headers,
            json=payload,
            stream=True,
            timeout=(connect_timeout_s, read_timeout_s),
        )
        on_response(r)
        log.info("OpenAI response status=%s content_type=%s", r.status_code, r.headers.get("content-type"))
        r.raise_for_status()

        for raw_line in r.iter_lines(decode_unicode=True):
            if stop_event.is_set():
                log.info("OpenAI stream cancelled by user")
                return
            if not raw_line:
                continue
            line = raw_line.strip()
            if not line.startswith("data:"):
                continue
            data = line[len("data:") :].strip()
            if data == "[DONE]":
                log.info("OpenAI stream done")
                return
            try:
                evt = json.loads(data)
            except Exception:
                log.debug("OpenAI stream json decode failed: %r", data[:200])
                continue
            try:
                delta = evt["choices"][0]["delta"]
                chunk = delta.get("content")
            except Exception:
                chunk = None
            if chunk:
                yield chunk
    finally:
        on_response(None)
        if r is not None:
            try:
                r.close()
            except Exception:
                pass


class TtsAudioWorker(threading.Thread):
    def __init__(self, ipc: IpcClient, tts: PiperTts, on_error: Callable[[str], None], log: logging.Logger):
        super().__init__(daemon=True)
        self._ipc = ipc
        self._tts = tts
        self._on_error = on_error
        self._log = log
        self._END_STREAM = object()
        self._STOP = object()
        self._q: "queue.Queue[object]" = queue.Queue()
        self._stop = threading.Event()
        self._active_stream = threading.Event()
        self._reported_tts_error = False

    def stop(self) -> None:
        self._stop.set()
        self._q.put(self._STOP)

    def cancel_current(self) -> None:
        while True:
            try:
                self._q.get_nowait()
            except queue.Empty:
                break
        if self._active_stream.is_set():
            try:
                self._ipc.audio_stream_begin()
                self._ipc.audio_stream_end()
            except Exception:
                pass
        self._active_stream.clear()

    def begin_stream(self) -> None:
        try:
            self._ipc.lip_sync_enable(True)
        except Exception:
            pass
        self._ipc.audio_stream_begin()
        self._active_stream.set()

    def end_stream(self) -> None:
        if self._active_stream.is_set():
            self._ipc.audio_stream_end()
        self._active_stream.clear()

    def enqueue_sentence(self, sentence: str) -> None:
        self._q.put(sentence)

    def enqueue_end_stream(self) -> None:
        self._q.put(self._END_STREAM)

    def run(self) -> None:
        tmp_dir = Path(os.environ.get("TMPDIR", "/tmp")) / "vrm-overlay-chat-tts"
        idx = 0
        max_buffer_s = 12.0
        sleep_s = 0.02
        while not self._stop.is_set():
            item = self._q.get()
            if item is self._STOP:
                return
            if item is self._END_STREAM:
                try:
                    self.end_stream()
                except Exception:
                    pass
                continue
            sentence = str(item)
            if not sentence.strip():
                continue
            idx += 1
            wav_path = tmp_dir / f"tts_{int(time.time())}_{idx}.wav"
            try:
                self._tts.synth_wav(sentence, wav_path)
                pcm = wav_path_to_pcm24k_mono_s16le(wav_path)
                # Keep each NDJSON command line well under the overlay's IPC read buffer (8192 bytes).
                # base64 expands by ~4/3, plus JSON overhead.
                chunk_size = 2048
                chunks_sent = 0
                for off in range(0, len(pcm), chunk_size):
                    if self._stop.is_set():
                        return
                    chunk = pcm[off : off + chunk_size]
                    self._ipc.audio_stream_chunk(chunk)
                    chunks_sent += 1
                    if (chunks_sent % 16) == 0:
                        st = self._ipc.audio_status()
                        if st and isinstance(st, dict):
                            avail_s = float(st.get("available_seconds", 0.0) or 0.0)
                            while avail_s > max_buffer_s and not self._stop.is_set():
                                time.sleep(sleep_s)
                                st = self._ipc.audio_status()
                                if not st or not isinstance(st, dict):
                                    break
                                avail_s = float(st.get("available_seconds", 0.0) or 0.0)
            except Exception as e:
                self._log.error("TTS failed: %s\n%s", str(e), traceback.format_exc())
                if not self._reported_tts_error:
                    self._reported_tts_error = True
                    self._on_error(
                        f"TTS failed: {e}\n"
                        f"Fix Settings → Piper model/bin.\n"
                        f"Log: {str(_log_path())}"
                    )
                continue


class SettingsDialog(tk.Toplevel):
    def __init__(self, parent: tk.Tk, settings: Settings):
        super().__init__(parent)
        self.title("Settings")
        self.resizable(False, False)
        self._settings = settings
        self._result: Optional[Settings] = None

        frm = ttk.Frame(self, padding=10)
        frm.grid(row=0, column=0, sticky="nsew")

        self.var_endpoint = tk.StringVar(value=settings.endpoint)
        self.var_api_key = tk.StringVar(value=settings.api_key)
        self.var_model = tk.StringVar(value=settings.model)
        self.var_system = tk.StringVar(value=settings.system_prompt)

        self.var_socket = tk.StringVar(value=settings.overlay_socket_path)
        self.var_vrm_path = tk.StringVar(value=settings.vrm_path)
        self.var_anim_path = tk.StringVar(value=settings.animation_path)
        self.var_theme = tk.StringVar(value=settings.theme)
        self.var_connect_timeout = tk.StringVar(value=str(settings.connect_timeout_s))
        self.var_read_timeout = tk.StringVar(value=str(settings.read_timeout_s))
        self.var_auto_blink = tk.BooleanVar(value=bool(settings.auto_blink))
        self.var_piper_bin = tk.StringVar(value=settings.piper_bin)
        self.var_piper_model = tk.StringVar(value=settings.piper_model)

        row = 0
        for label, var, show in [
            ("Endpoint", self.var_endpoint, None),
            ("API key", self.var_api_key, "*"),
            ("Model", self.var_model, None),
            ("System prompt", self.var_system, None),
            ("Overlay socket", self.var_socket, None),
            ("VRM model path", self.var_vrm_path, None),
            ("Animation path", self.var_anim_path, None),
        ]:
            ttk.Label(frm, text=label).grid(row=row, column=0, sticky="w", pady=2)
            ent = ttk.Entry(frm, textvariable=var, width=52, show=show) if show else ttk.Entry(frm, textvariable=var, width=52)
            ent.grid(row=row, column=1, sticky="w", pady=2)
            row += 1

        ttk.Label(frm, text="Theme").grid(row=row, column=0, sticky="w", pady=2)
        theme_box = ttk.Combobox(frm, textvariable=self.var_theme, width=49, values=[THEME_AUTO, THEME_LIGHT, THEME_DARK], state="readonly")
        theme_box.grid(row=row, column=1, sticky="w", pady=2)
        row += 1

        for label, var in [
            ("Connect timeout (s)", self.var_connect_timeout),
            ("Read timeout (s)", self.var_read_timeout),
        ]:
            ttk.Label(frm, text=label).grid(row=row, column=0, sticky="w", pady=2)
            ent = ttk.Entry(frm, textvariable=var, width=52)
            ent.grid(row=row, column=1, sticky="w", pady=2)
            row += 1

        ttk.Checkbutton(frm, text="Enable auto-blink", variable=self.var_auto_blink).grid(
            row=row, column=1, sticky="w", pady=2
        )
        row += 1

        for label, var in [
            ("Piper bin", self.var_piper_bin),
            ("Piper model", self.var_piper_model),
        ]:
            ttk.Label(frm, text=label).grid(row=row, column=0, sticky="w", pady=2)
            ent = ttk.Entry(frm, textvariable=var, width=52)
            ent.grid(row=row, column=1, sticky="w", pady=2)
            row += 1

        cfg = str(_config_path())
        ttk.Label(frm, text="Saved to").grid(row=row, column=0, sticky="w", pady=(10, 2))
        ttk.Label(frm, text=cfg).grid(row=row, column=1, sticky="w", pady=(10, 2))
        row += 1

        btns = ttk.Frame(frm)
        btns.grid(row=row, column=0, columnspan=2, sticky="e", pady=(10, 0))
        ttk.Button(btns, text="Cancel", command=self._on_cancel).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(btns, text="Save", command=self._on_save).grid(row=0, column=1)

        self.bind("<Escape>", lambda _e: self._on_cancel())
        self.bind("<Return>", lambda _e: self._on_save())
        self.grab_set()
        self.transient(parent)

    def _on_cancel(self) -> None:
        self._result = None
        self.destroy()

    def _on_save(self) -> None:
        def parse_float(s: str, default: float) -> float:
            try:
                v = float(s)
                if v <= 0:
                    return default
                return v
            except Exception:
                return default

        self._result = Settings(
            endpoint=self.var_endpoint.get().strip() or DEFAULT_ENDPOINT,
            api_key=self.var_api_key.get().strip(),
            model=self.var_model.get().strip() or DEFAULT_MODEL,
            system_prompt=self.var_system.get().strip() or DEFAULT_SYSTEM_PROMPT,
            overlay_socket_path=self.var_socket.get().strip() or DEFAULT_SOCKET_PATH,
            vrm_path=self.var_vrm_path.get().strip() or DEFAULT_VRM_PATH,
            animation_path=self.var_anim_path.get().strip() or DEFAULT_ANIM_PATH,
            theme=self.var_theme.get().strip() or THEME_AUTO,
            connect_timeout_s=parse_float(self.var_connect_timeout.get().strip(), DEFAULT_CONNECT_TIMEOUT_S),
            read_timeout_s=parse_float(self.var_read_timeout.get().strip(), DEFAULT_READ_TIMEOUT_S),
            auto_blink=bool(self.var_auto_blink.get()),
            piper_bin=self.var_piper_bin.get().strip() or "piper",
            piper_model=self.var_piper_model.get().strip(),
        )
        self.destroy()

    @property
    def result(self) -> Optional[Settings]:
        return self._result


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("vrm-overlay Chat + TTS")
        self.root.geometry("860x560")

        self.log = setup_logger()

        self.config_path = _config_path()
        self.settings = Settings.load(self.config_path)
        if not self.settings.api_key:
            self.settings.api_key = os.environ.get("OPENAI_API_KEY", "")

        self._colors = apply_theme(self.root, self.settings.theme)

        self._ipc = IpcClient(self.settings.overlay_socket_path)
        self._tts_worker = TtsAudioWorker(
            self._ipc,
            PiperTts(self.settings.piper_bin, self.settings.piper_model),
            on_error=lambda msg: self._ui(self._append_text, f"\n\n[error] {msg}\n"),
            log=self.log,
        )
        self._tts_worker.start()

        self._chat_thread: Optional[threading.Thread] = None
        self._chat_lock = threading.Lock()
        self._chat_active_id = 0
        self._chat_stop: Optional[threading.Event] = None
        self._chat_response: Optional[Response] = None

        self._messages: list[dict] = [{"role": "system", "content": self.settings.system_prompt}]

        self._build_ui()

    def _ui(self, fn, *args) -> None:
        self.root.after(0, fn, *args)

    def _build_ui(self) -> None:
        top = ttk.Frame(self.root, padding=8)
        top.pack(fill="both", expand=True)
        top.columnconfigure(0, weight=1)
        top.rowconfigure(1, weight=1)

        toolbar = ttk.Frame(top)
        toolbar.grid(row=0, column=0, sticky="ew")
        ttk.Button(toolbar, text="Settings", command=self._open_settings).grid(row=0, column=0, sticky="w")
        ttk.Button(toolbar, text="Connect overlay", command=self._connect_overlay).grid(row=0, column=1, sticky="w", padx=(8, 0))
        ttk.Button(toolbar, text="Reload model/anim", command=self._reload_model_anim).grid(row=0, column=2, sticky="w", padx=(8, 0))
        ttk.Button(toolbar, text="Stop", command=self._stop_current).grid(row=0, column=3, sticky="w", padx=(8, 0))
        ttk.Button(toolbar, text="Open log", command=self._open_log).grid(row=0, column=4, sticky="w", padx=(8, 0))
        toolbar.columnconfigure(5, weight=1)

        self.status = tk.StringVar(value="Idle")
        ttk.Label(toolbar, textvariable=self.status).grid(row=0, column=6, sticky="e")

        self.transcript = tk.Text(top, wrap="word", height=20)
        self.transcript.grid(row=1, column=0, sticky="nsew", pady=(8, 8))
        self.transcript.configure(
            bg=self._colors["entry_bg"],
            fg=self._colors["fg"],
            insertbackground=self._colors["fg"],
            selectbackground=self._colors["select_bg"],
        )
        self.transcript.configure(state="disabled")

        bottom = ttk.Frame(top)
        bottom.grid(row=2, column=0, sticky="ew")
        bottom.columnconfigure(0, weight=1)

        self.entry = ttk.Entry(bottom)
        self.entry.grid(row=0, column=0, sticky="ew")
        self.entry.bind("<Return>", lambda _e: self._on_send())

        ttk.Button(bottom, text="Send", command=self._on_send).grid(row=0, column=1, sticky="e", padx=(8, 0))

    def _reload_model_anim(self) -> None:
        try:
            vrm_path = str(Path(self.settings.vrm_path).expanduser())
            anim_path = str(Path(self.settings.animation_path).expanduser())
            self._ipc.load_model(vrm_path)
            if anim_path:
                self._ipc.load_animation(anim_path)
                self._ipc.blend_to(0, duration=0.15, loop=True)
                self._ipc.play()
            if self.settings.auto_blink:
                self._ipc.auto_blink_enable(True)
            self._set_status("Model/animation loaded")
        except Exception as e:
            messagebox.showerror("Load failed", str(e))

    def _open_log(self) -> None:
        try:
            path = str(_log_path())
            if sys.platform == "darwin":
                subprocess.run(["open", path], check=False)
            elif sys.platform.startswith("win"):
                os.startfile(path)  # type: ignore[attr-defined]
            else:
                subprocess.run(["xdg-open", path], check=False)
        except Exception:
            self._append_text(f"\n[debug] log path: {str(_log_path())}\n")

    def _append_text(self, text: str) -> None:
        self.transcript.configure(state="normal")
        self.transcript.insert("end", text)
        self.transcript.see("end")
        self.transcript.configure(state="disabled")

    def _set_status(self, text: str) -> None:
        self.status.set(text)

    def _connect_overlay(self) -> None:
        try:
            self._ipc.socket_path = self.settings.overlay_socket_path
            self._ipc.close()
            self._ipc.connect()
            self._set_status(f"Connected: {self.settings.overlay_socket_path} (loading model/anim…)")

            vrm_path = str(Path(self.settings.vrm_path).expanduser())
            anim_path = str(Path(self.settings.animation_path).expanduser())
            self._ipc.load_model(vrm_path)
            if anim_path:
                self._ipc.load_animation(anim_path)
                self._ipc.blend_to(0, duration=0.15, loop=True)
                self._ipc.play()
            if self.settings.auto_blink:
                self._ipc.auto_blink_enable(True)

            self._set_status(f"Connected: {self.settings.overlay_socket_path}")
        except Exception as e:
            messagebox.showerror("Overlay connection failed", str(e))
            self._set_status("Overlay disconnected")

    def _open_settings(self) -> None:
        dlg = SettingsDialog(self.root, self.settings)
        self.root.wait_window(dlg)
        if dlg.result is None:
            return

        self.settings = dlg.result
        self.settings.save(self.config_path)

        self._colors = apply_theme(self.root, self.settings.theme)
        self.transcript.configure(
            bg=self._colors["entry_bg"],
            fg=self._colors["fg"],
            insertbackground=self._colors["fg"],
            selectbackground=self._colors["select_bg"],
        )

        self._ipc.socket_path = self.settings.overlay_socket_path
        self._ipc.close()

        self._tts_worker.cancel_current()
        self._tts_worker.stop()
        self._tts_worker = TtsAudioWorker(
            self._ipc,
            PiperTts(self.settings.piper_bin, self.settings.piper_model),
            on_error=lambda msg: self._ui(self._append_text, f"\n\n[error] {msg}\n"),
            log=self.log,
        )
        self._tts_worker.start()

        self._messages = [{"role": "system", "content": self.settings.system_prompt}]
        self._append_text("\n[settings updated]\n")

    def _stop_current(self, quiet: bool = False) -> None:
        with self._chat_lock:
            stop_evt = self._chat_stop
            resp = self._chat_response
            active = bool(self._chat_thread and self._chat_thread.is_alive())
        if stop_evt is not None:
            stop_evt.set()
        if resp is not None:
            try:
                resp.close()
            except Exception:
                pass
        self._tts_worker.cancel_current()
        self._set_status("Stopped")
        if active and not quiet:
            self.log.info("Stop requested by user")
            self._append_text(f"\n[debug] stop requested; log: {str(_log_path())}\n")

    def _on_send(self) -> None:
        user_text = self.entry.get().strip()
        if not user_text:
            return
        with self._chat_lock:
            if self._chat_thread and self._chat_thread.is_alive() and self._chat_stop and not self._chat_stop.is_set():
                messagebox.showinfo("Busy", "Wait for the current response or press Stop.")
                return

        self.entry.delete(0, "end")
        self._append_text(f"\nYou: {user_text}\n")
        self._append_text("Assistant: ")

        # If a previous request thread is stuck, cancel it and start a new one anyway.
        self._stop_current(quiet=True)

        with self._chat_lock:
            self._chat_active_id += 1
            chat_id = self._chat_active_id
            self._chat_stop = threading.Event()
            self._chat_response = None

        self._chat_thread = threading.Thread(target=self._run_chat, args=(chat_id, user_text), daemon=True)
        self._chat_thread.start()

    def _set_chat_response(self, chat_id: int, resp: Optional[Response]) -> None:
        with self._chat_lock:
            if chat_id != self._chat_active_id:
                if resp is not None:
                    try:
                        resp.close()
                    except Exception:
                        pass
                return
            self._chat_response = resp

    def _run_chat(self, chat_id: int, user_text: str) -> None:
        with self._chat_lock:
            stop_evt = self._chat_stop or threading.Event()
        try:
            self.log.info("Chat request start id=%d len=%d", chat_id, len(user_text))
            self._ui(self._set_status, "Requesting…")
            self._messages.append({"role": "user", "content": user_text})

            seg = SentenceSegmenter()
            assistant_accum = ""

            try:
                self._tts_worker.begin_stream()
            except Exception:
                pass

            for delta in iter_chat_completions_stream(
                endpoint=self.settings.endpoint,
                api_key=self.settings.api_key,
                model=self.settings.model,
                messages=self._messages,
                connect_timeout_s=self.settings.connect_timeout_s,
                read_timeout_s=self.settings.read_timeout_s,
                log=self.log,
                stop_event=stop_evt,
                on_response=lambda r: self._set_chat_response(chat_id, r),
            ):
                if stop_evt is not None and stop_evt.is_set():
                    return
                assistant_accum += delta
                self._ui(self._append_text, delta)

                for sentence in seg.push(delta):
                    if stop_evt is not None and stop_evt.is_set():
                        return
                    cleaned = clean_for_tts(sentence)
                    if cleaned:
                        self._tts_worker.enqueue_sentence(cleaned)

            tail = seg.flush()
            if tail:
                cleaned = clean_for_tts(tail)
                if cleaned:
                    self._tts_worker.enqueue_sentence(cleaned)
            self._tts_worker.enqueue_end_stream()

            self._messages.append({"role": "assistant", "content": assistant_accum})
            self._ui(self._append_text, "\n")
            self._ui(self._set_status, "Idle")
        except Exception as e:
            self.log.error("Chat request failed: %s\n%s", str(e), traceback.format_exc())
            self._ui(self._append_text, f"\n\n[error] {e}\n[debug] log: {str(_log_path())}\n")
            self._ui(messagebox.showerror, "Error", f"{e}\n\nLog: {str(_log_path())}")
            self._ui(self._set_status, "Error")
        finally:
            self.log.info("Chat request end id=%d stopped=%s", chat_id, stop_evt.is_set() if stop_evt else None)
            with self._chat_lock:
                if chat_id == self._chat_active_id:
                    self._chat_response = None
                    if self._chat_stop is not None and self._chat_stop.is_set():
                        self._ui(self._set_status, "Stopped")


def main() -> None:
    root = tk.Tk()
    style = ttk.Style(root)
    if "clam" in style.theme_names():
        style.theme_use("clam")

    app = App(root)
    def on_close() -> None:
        try:
            app._stop_current()
            app._tts_worker.stop()
            app._ipc.close()
        finally:
            root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.mainloop()


if __name__ == "__main__":
    main()
