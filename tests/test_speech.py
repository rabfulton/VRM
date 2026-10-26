import logging
from pathlib import Path
import struct
import tempfile
import threading
import unittest
import wave

from vrm_overlay.speech import TtsAudioWorker, wav_path_to_pcm24k_mono_s16le


class FakeIpc:
    def __init__(self):
        self.chunks = []
        self.cancel_count = 0
        self.got_chunk = threading.Event()
        self.ended = threading.Event()
        self.drained = True

    def audio_stream_begin(self):
        pass

    def audio_stream_chunk(self, pcm):
        self.chunks.append(pcm)
        self.got_chunk.set()

    def audio_stream_end(self):
        self.ended.set()

    def audio_cancel(self):
        self.cancel_count += 1

    def audio_status(self):
        return {"active": True, "available_seconds": 0, "drained": self.drained}


class FakeTts:
    def __init__(self):
        self.started = threading.Event()
        self.release = threading.Event()

    def synth_wav(self, text, path, stop_event=None):
        if text == "old":
            self.started.set()
            if not self.release.wait(3):
                raise TimeoutError("Test did not release synthesis")
            # Deliberately return a late result even after cancellation.
        with wave.open(str(path), "wb") as wav:
            wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
            wav.writeframes(struct.pack("<h", 100 if text == "new" else 200) * 256)


class SpeechTests(unittest.TestCase):
    def worker(self):
        ipc, tts = FakeIpc(), FakeTts()
        self.errors = []
        self.idle = threading.Event()
        log = logging.getLogger("speech-test")
        log.addHandler(logging.NullHandler())
        worker = TtsAudioWorker(ipc, tts, self.errors.append, log,
                                on_state=lambda state: self.idle.set() if state == "Idle" else None)
        def cleanup():
            tts.release.set()
            worker.stop()
            if worker.ident:
                worker.join(timeout=3)
                self.assertFalse(worker.is_alive())
        self.addCleanup(cleanup)
        return worker, ipc, tts

    def test_cancel_discards_late_synthesis_and_old_enqueues(self):
        worker, ipc, tts = self.worker()
        worker.start()
        old = worker.begin_stream()
        worker.enqueue_sentence("old", old)
        self.assertTrue(tts.started.wait(2))
        worker.cancel_current()
        new = worker.begin_stream()
        worker.enqueue_sentence("stale producer", old)
        worker.enqueue_end_stream(old)
        worker.enqueue_sentence("new", new)
        worker.enqueue_end_stream(new)
        tts.release.set()
        self.assertTrue(self.idle.wait(3))
        self.assertEqual(ipc.cancel_count, 1)
        self.assertEqual(b"".join(ipc.chunks), struct.pack("<h", 100) * 256)
        self.assertFalse(self.errors)

    def test_stop_still_cancels_audio_after_end_ack(self):
        worker, ipc, _ = self.worker()
        ipc.drained = False
        worker.start()
        token = worker.begin_stream()
        worker.enqueue_sentence("new", token)
        worker.enqueue_end_stream(token)
        self.assertTrue(ipc.ended.wait(2))
        worker.cancel_current()
        self.assertEqual(ipc.cancel_count, 1)
        self.assertFalse(self.idle.is_set())

    def test_cancel_releases_producer_waiting_on_full_queue(self):
        worker, _, _ = self.worker()
        token = worker.begin_stream()
        for _ in range(32):
            worker.enqueue_sentence("queued", token)
        producer = threading.Thread(target=worker.enqueue_sentence, args=("blocked", token))
        producer.start()
        worker.cancel_current()
        producer.join(timeout=2)
        self.assertFalse(producer.is_alive())
        self.assertTrue(worker._q.empty())

    def test_stopped_chat_cannot_start_new_audio(self):
        worker, _, _ = self.worker()
        stopped = threading.Event()
        stopped.set()
        self.assertIsNone(worker.begin_stream(stopped))
        worker.stop()
        self.assertIsNone(worker.begin_stream())

    def test_stereo_resampling(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stereo.wav"
            with wave.open(str(path), "wb") as wav:
                wav.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
                wav.writeframes(struct.pack("<hh", 1000, 3000) * 480)
            pcm = wav_path_to_pcm24k_mono_s16le(path)
            self.assertEqual(pcm, struct.pack("<h", 2000) * 240)


class PiperProcessTests(unittest.TestCase):
    def test_cancel_terminates_synthesis_subprocess(self):
        from vrm_overlay.speech import PiperTts
        import time
        import sys
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / "voice.onnx"
            model.touch()
            Path(str(model) + ".json").write_text("{}")
            executable = Path(directory) / "fake-piper"
            executable.write_text(f"#!{sys.executable}\nimport time\ntime.sleep(30)\n")
            executable.chmod(0o700)
            cancel = threading.Event()
            timer = threading.Timer(0.1, cancel.set)
            timer.start()
            start = time.monotonic()
            try:
                with self.assertRaisesRegex(RuntimeError, "cancelled"):
                    PiperTts(str(executable), str(model)).synth_wav("hello", Path(directory) / "out.wav", stop_event=cancel)
            finally:
                timer.cancel()
                timer.join()
            self.assertLess(time.monotonic() - start, 2)

    def test_synthesis_failure_reports_stderr(self):
        from vrm_overlay.speech import PiperTts
        import sys
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / "voice.onnx"
            model.touch()
            Path(str(model) + ".json").write_text("{}")
            executable = Path(directory) / "fake-piper"
            executable.write_text(f"#!{sys.executable}\nimport sys\nprint('bad voice', file=sys.stderr)\nsys.exit(2)\n")
            executable.chmod(0o700)
            with self.assertRaisesRegex(RuntimeError, "bad voice"):
                PiperTts(str(executable), str(model)).synth_wav("hello", Path(directory) / "out.wav")
