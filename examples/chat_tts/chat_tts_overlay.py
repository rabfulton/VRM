from __future__ import annotations

import json
import logging
import os
import queue
import re
import subprocess
import sys
import threading
import time
import traceback
from dataclasses import asdict, dataclass, field
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
sys.path.insert(0, str(REPO_ROOT))
from vrm_overlay import IpcClient
from vrm_overlay.speech import TtsAudioWorker
from vrm_overlay.tts import SpeechConfig, create_tts
from vrm_overlay.credentials import CredentialError, SystemCredentials

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
    api_key: str = field(default="", repr=False)
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
    tts: dict = field(default_factory=lambda: asdict(SpeechConfig()))

    @classmethod
    def load(cls, path: Path) -> "Settings":
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            allowed = {f.name for f in dataclass_fields(cls)}
            merged = {**asdict(cls()), **{k: v for (k, v) in raw.items() if k in allowed}}
            # Preserve an explicitly configured Piper voice from older settings.
            if "tts" not in raw and raw.get("piper_model"):
                merged["tts"] = asdict(SpeechConfig(provider="piper", model="piper",
                                                   executable=merged["piper_bin"], model_path=merged["piper_model"]))
            merged["tts"] = asdict(SpeechConfig.from_dict(merged["tts"]))
            settings = cls(**merged)
            if settings.api_key:
                # Migrate the old prototype's plaintext key only after the OS
                # confirms storage. A locked keyring leaves the old file intact.
                settings.save(path)
            return settings
        except FileNotFoundError:
            return cls()
        except CredentialError:
            raise
        except Exception:
            return cls()

    def save(self, path: Path) -> None:
        if self.api_key:
            SystemCredentials().set(self.endpoint, self.api_key)
        values = asdict(self)
        values.pop("api_key", None)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(values, indent=2, sort_keys=True) + "\n", encoding="utf-8")


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
        speech = SpeechConfig.from_dict(settings.tts)
        self.var_tts_provider = tk.StringVar(value=speech.provider)
        self.var_tts_model = tk.StringVar(value=speech.model)
        self.var_tts_device = tk.StringVar(value=speech.device)
        self.var_piper_bin = tk.StringVar(value=speech.executable)
        self.var_piper_model = tk.StringVar(value=speech.model_path)
        self.var_tts_codec = tk.StringVar(value=speech.codec_path)
        self.var_tts_voice = tk.StringVar(value=speech.voice)

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

        for label, var, choices in [
            ("Speech provider", self.var_tts_provider, ["crispasr", "piper"]),
            ("Speech model/backend", self.var_tts_model, ["chatterbox-nano", "chatterbox-turbo", "kokoro"]),
            ("Speech processing", self.var_tts_device, ["cpu", "vulkan", "auto"]),
        ]:
            ttk.Label(frm, text=label).grid(row=row, column=0, sticky="w", pady=2)
            ttk.Combobox(frm, textvariable=var, values=choices, width=49).grid(row=row, column=1, sticky="w", pady=2)
            row += 1

        for label, var in [
            ("Speech executable (blank = automatic)", self.var_piper_bin),
            ("Speech model file (blank = automatic)", self.var_piper_model),
            ("Speech decoder file (optional)", self.var_tts_codec),
            ("Speech voice pack/name (optional)", self.var_tts_voice),
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

        try:
            speech = SpeechConfig.from_dict(dict(
                provider=self.var_tts_provider.get().strip(),
                model=self.var_tts_model.get().strip() or "chatterbox-nano",
                device=self.var_tts_device.get().strip(),
                executable=self.var_piper_bin.get().strip(),
                model_path=self.var_piper_model.get().strip(),
                codec_path=self.var_tts_codec.get().strip(),
                voice=self.var_tts_voice.get().strip()))
        except ValueError as error:
            messagebox.showerror("Speech settings", str(error), parent=self)
            return
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
            tts=asdict(speech),
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
            try:
                self.settings.api_key = SystemCredentials().get(self.settings.endpoint)
            except CredentialError:
                pass  # Settings can still be opened to configure a local API.
        if not self.settings.api_key:
            self.settings.api_key = os.environ.get("OPENAI_API_KEY", "")

        self._colors = apply_theme(self.root, self.settings.theme)

        self._ui_queue = queue.Queue()
        self._ipc = IpcClient(self.settings.overlay_socket_path)
        self._tts_worker = TtsAudioWorker(
            self._ipc,
            create_tts(SpeechConfig.from_dict(self.settings.tts)),
            on_error=lambda msg: self._ui(self._append_text, f"\n\n[error] {msg}\n"),
            log=self.log,
            on_state=lambda state: self._ui(self._set_status, state),
        )
        self._tts_worker.start()

        self._chat_thread: Optional[threading.Thread] = None
        self._chat_lock = threading.Lock()
        self._chat_active_id = 0
        self._chat_stop: Optional[threading.Event] = None
        self._chat_response: Optional[Response] = None

        self._messages: list[dict] = [{"role": "system", "content": self.settings.system_prompt}]

        self._build_ui()
        self.root.after(25, self._drain_ui)

    def _ui(self, fn, *args) -> None:
        # Worker threads never call Tk, including root.after (which can block).
        self._ui_queue.put((fn, args))

    def _drain_ui(self) -> None:
        try:
            for _ in range(100):
                try:
                    fn, args = self._ui_queue.get_nowait()
                except queue.Empty:
                    break
                fn(*args)
        finally:
            self.root.after(25, self._drain_ui)

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

        try:
            dlg.result.save(self.config_path)
        except CredentialError as error:
            messagebox.showerror("System keyring", str(error))
            return
        self.settings = dlg.result

        self._colors = apply_theme(self.root, self.settings.theme)
        self.transcript.configure(
            bg=self._colors["entry_bg"],
            fg=self._colors["fg"],
            insertbackground=self._colors["fg"],
            selectbackground=self._colors["select_bg"],
        )

        self._stop_current(quiet=True)
        self._ipc.close()
        self._ipc.socket_path = self.settings.overlay_socket_path

        self._tts_worker.cancel_current()
        self._tts_worker.stop()
        self._tts_worker = TtsAudioWorker(
            self._ipc,
            create_tts(SpeechConfig.from_dict(self.settings.tts)),
            on_error=lambda msg: self._ui(self._append_text, f"\n\n[error] {msg}\n"),
            log=self.log,
            on_state=lambda state: self._ui(self._set_status, state),
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
            if chat_id != self._chat_active_id:
                return
            stop_evt = self._chat_stop or threading.Event()
            speech_worker = self._tts_worker
            settings = self.settings
            self._messages.append({"role": "user", "content": user_text})
            messages = list(self._messages)
        speech_token = None
        try:
            self.log.info("Chat request start id=%d len=%d", chat_id, len(user_text))
            self._ui(self._set_status, "Requesting…")
            seg = SentenceSegmenter()
            assistant_accum = ""

            try:
                speech_token = speech_worker.begin_stream(stop_evt)
            except Exception as error:
                self._ui(self._append_text, f"\n[speech unavailable] {error}\n")

            for delta in iter_chat_completions_stream(
                endpoint=settings.endpoint,
                api_key=settings.api_key,
                model=settings.model,
                messages=messages,
                connect_timeout_s=settings.connect_timeout_s,
                read_timeout_s=settings.read_timeout_s,
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
                        speech_worker.enqueue_sentence(cleaned, speech_token)

            if stop_evt.is_set():
                return
            tail = seg.flush()
            if tail:
                cleaned = clean_for_tts(tail)
                if cleaned:
                    speech_worker.enqueue_sentence(cleaned, speech_token)
            speech_worker.enqueue_end_stream(speech_token)

            with self._chat_lock:
                if chat_id != self._chat_active_id or stop_evt.is_set():
                    return
                self._messages.append({"role": "assistant", "content": assistant_accum})
            self._ui(self._append_text, "\n")
            self._ui(self._set_status, "Response ready")
        except Exception as e:
            if stop_evt.is_set():
                return
            if speech_token is not None:
                speech_worker.cancel_current(speech_token)
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
