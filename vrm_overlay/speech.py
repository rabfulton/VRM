"""Bounded sentence synthesis/playback worker independent of the chat UI/provider."""
from __future__ import annotations

import logging
from pathlib import Path
import queue
import subprocess
import sys
import time
import tempfile
import threading
import wave
from typing import Callable

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

    if sys.byteorder != "little":
        samples_i16.byteswap()
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

    def synth_wav(self, text: str, out_path: Path, timeout_s: float = 60.0,
                  stop_event: threading.Event | None = None) -> None:
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
        process = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + timeout_s
        input_bytes = text.encode("utf-8")
        try:
            while True:
                if stop_event is not None and stop_event.is_set():
                    raise RuntimeError("Speech synthesis cancelled")
                if time.monotonic() >= deadline:
                    raise TimeoutError("Piper synthesis timed out")
                try:
                    _, stderr = process.communicate(input=input_bytes, timeout=0.05)
                    if process.returncode:
                        raise RuntimeError(f"Piper exited with status {process.returncode}: {stderr.decode(errors='replace')[-2000:]}")
                    return
                except subprocess.TimeoutExpired:
                    input_bytes = None
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.communicate(timeout=1)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()


class TtsAudioWorker(threading.Thread):
    """Only the current stream generation may enqueue or send audio.

    Synthesis runs outside the state lock. Cancel invalidates its result immediately;
    a late provider result is discarded before any PCM reaches the runtime.
    """

    def __init__(self, ipc, tts, on_error: Callable[[str], None], log: logging.Logger,
                 on_state: Callable[[str], None] = lambda _state: None):
        super().__init__(daemon=True)
        self._ipc = ipc
        self._tts = tts
        self._on_error = on_error
        self._log = log
        self._on_state = on_state
        self._state_lock = threading.Lock()
        self._shutdown = threading.Event()
        self._generation = 0
        self._cancelled = threading.Event()
        self._active = False
        self._q = queue.Queue(maxsize=32)
        self._END_STREAM = object()

    def _current(self, token) -> bool:
        return token is not None and token == self._generation and not self._shutdown.is_set() and self._active

    def _discard_queue(self) -> None:
        while True:
            try:
                self._q.get_nowait()
            except queue.Empty:
                return

    def cancel_current(self, token=None) -> None:
        with self._state_lock:
            if token is not None and token != self._generation:
                return
            self._cancelled.set()
            self._generation += 1
            self._discard_queue()
            if self._active:
                try:
                    self._ipc.audio_cancel()
                except Exception as error:
                    self._log.warning("Audio cancellation failed: %s", error)
            self._active = False

    def stop(self) -> None:
        self._shutdown.set()
        self.cancel_current()

    def begin_stream(self, stop_event: threading.Event | None = None) -> int | None:
        with self._state_lock:
            if self._shutdown.is_set() or (stop_event is not None and stop_event.is_set()):
                return None
            self._cancelled.set()
            self._generation += 1
            self._discard_queue()
            self._active = False
            self._ipc.audio_stream_begin()
            self._cancelled = threading.Event()
            self._active = True
            return self._generation

    def _enqueue(self, item, token) -> None:
        while not self._shutdown.is_set():
            with self._state_lock:
                if not self._current(token):
                    return
                try:
                    self._q.put_nowait((token, item))
                    return
                except queue.Full:
                    pass
            self._shutdown.wait(0.02)

    def enqueue_sentence(self, sentence: str, token) -> None:
        if sentence.strip():
            self._enqueue(sentence, token)

    def enqueue_end_stream(self, token) -> None:
        self._enqueue(self._END_STREAM, token)

    def _finish(self, token) -> None:
        with self._state_lock:
            if not self._current(token):
                return
            self._ipc.audio_stream_end()
        while not self._shutdown.is_set():
            with self._state_lock:
                if not self._current(token):
                    return
                status = self._ipc.audio_status()
                if status.get("drained"):
                    self._active = False
                    self._on_state("Idle")
                    return
                if not status.get("active"):
                    raise RuntimeError("Audio playback device is unavailable")
            self._shutdown.wait(0.03)

    def run(self) -> None:
        try:
            self._run()
        finally:
            if hasattr(self._tts, "close"):
                self._tts.close()

    def _run(self) -> None:
        with tempfile.TemporaryDirectory(prefix="vrm-overlay-tts-") as tmp_dir:
            wav_path = Path(tmp_dir) / "sentence.wav"
            while not self._shutdown.is_set():
                try:
                    token, item = self._q.get(timeout=0.1)
                except queue.Empty:
                    continue
                try:
                    with self._state_lock:
                        if not self._current(token):
                            continue
                        cancelled = self._cancelled
                    if item is self._END_STREAM:
                        self._finish(token)
                        continue
                    self._tts.synth_wav(str(item), wav_path, stop_event=cancelled)
                    with self._state_lock:
                        if not self._current(token):
                            continue
                    pcm = wav_path_to_pcm24k_mono_s16le(wav_path)
                    for off in range(0, len(pcm), 2048):
                        # Bound queued playback to one second so speech stays responsive.
                        while not self._shutdown.is_set():
                            with self._state_lock:
                                if not self._current(token):
                                    break
                                status = self._ipc.audio_status()
                                if not status.get("active"):
                                    raise RuntimeError("Audio playback device is unavailable")
                                if float(status.get("available_seconds", 0)) < 1.0:
                                    self._ipc.audio_stream_chunk(pcm[off:off + 2048])
                                    self._on_state("Speaking")
                                    break
                            self._shutdown.wait(0.02)
                        with self._state_lock:
                            if not self._current(token):
                                break
                except Exception as error:
                    with self._state_lock:
                        if not self._current(token):
                            continue
                        self._log.exception("Speech failed")
                        try:
                            self._ipc.audio_cancel()
                        except Exception:
                            pass
                        self._active = False
                        self._discard_queue()
                        self._on_error(f"Speech failed: {error}")
                finally:
                    wav_path.unlink(missing_ok=True)
