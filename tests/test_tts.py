"""Real sidecar lifecycle/HTTP tests using a tiny native-contract fixture."""
from dataclasses import asdict, replace
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from vrm_overlay.tts import CrispAsrTts, SpeechConfig, SpeechPreferences, create_tts
from vrm_overlay import tts_install


FAKE_CRISPASR = '''
import http.server, io, json, os, pathlib, struct, sys, time, wave
args=sys.argv[1:]
pathlib.Path(__file__).with_suffix('.args').write_text(json.dumps(args))
port=int(args[args.index('--port')+1])
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_GET(self):
        self.send_response(200); self.end_headers()
        self.wfile.write(b'{"status":"ok"}')
    def do_POST(self):
        assert self.headers['Authorization']=='Bearer '+os.environ['CRISPASR_API_KEYS']
        data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if data['input']=='slow': time.sleep(30)
        if data['input']=='error':
            self.send_response(400);self.end_headers();self.wfile.write(b'bad voice');return
        output=io.BytesIO()
        with wave.open(output,'wb') as wav:
            wav.setparams((1,2,24000,0,'NONE','not compressed'))
            wav.writeframes(struct.pack('<h',1200)*2400)
        self.send_response(200);self.end_headers();self.wfile.write(output.getvalue())
http.server.ThreadingHTTPServer(('127.0.0.1',port),Handler).serve_forever()
'''


class TtsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="vrm speech tests ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        models = patch("vrm_overlay.tts_models.prepare_model_files", return_value=None)
        models.start()
        self.addCleanup(models.stop)
        self.executable = self.root / "fake crispasr"
        self.executable.write_text(f"#!{sys.executable}\n" + FAKE_CRISPASR)
        self.executable.chmod(0o700)
        self.engine = CrispAsrTts(SpeechConfig(executable=str(self.executable)))
        self.addCleanup(self.engine.close)

    def test_default_nano_and_independent_profiles(self):
        preferences = SpeechPreferences()
        self.assertEqual(preferences.config, SpeechConfig())
        self.assertEqual(preferences.config.device, "vulkan")
        preferences.update(model_path="nano.gguf", voice="nano-voice.gguf", device="cpu")
        preferences.select("crispasr", "chatterbox-turbo")
        self.assertEqual(preferences.config.model_path, "")
        preferences.update(model_path="turbo.gguf")
        preferences.select("piper", "piper")
        preferences.update(model_path="voice.onnx")
        preferences.select("crispasr", "chatterbox-nano")
        restored = SpeechPreferences(json.loads(json.dumps(preferences.to_dict())))
        self.assertEqual(restored.config.model_path, "nano.gguf")
        self.assertEqual(restored.config.device, "cpu")
        self.assertEqual(restored.config.voice, "nano-voice.gguf")
        restored.select("piper", "piper")
        self.assertEqual(restored.config.model_path, "voice.onnx")

    def test_persistent_server_reused_and_cli_arguments(self):
        output = self.root / "output.wav"
        self.engine.synth_wav("hello", output)
        process = self.engine._process
        self.assertTrue(output.read_bytes().startswith(b"RIFF"))
        self.engine.synth_wav("again", output)
        self.assertIs(process, self.engine._process)
        args = json.loads(self.executable.with_suffix(".args").read_text())
        self.assertEqual(args[args.index("--backend")+1], "chatterbox-nano")
        self.assertEqual(args[args.index("--host")+1], "127.0.0.1")
        self.assertEqual(args[args.index("--gpu-backend")+1], "vulkan")
        self.assertNotIn("-ng", args)
        self.assertIn("--auto-download", args)

    def test_vulkan_and_local_models_are_passed_as_arguments(self):
        model, codec = self.root / "model file.gguf", self.root / "decoder file.gguf"
        model.touch();codec.touch()
        engine = CrispAsrTts(replace(self.engine.config, model="chatterbox-turbo", device="vulkan", gpu_device=1,
                                    model_path=str(model), codec_path=str(codec)))
        self.addCleanup(engine.close)
        command = engine._command(12345)
        self.assertIn(str(model), command)
        self.assertIn(str(codec), command)
        self.assertNotIn("--auto-download", command)
        cpu = CrispAsrTts(replace(self.engine.config, device="cpu"))
        self.assertIn("-ng", cpu._command(12345))
        self.assertEqual(command[command.index("--gpu-backend")+1], "vulkan")
        self.assertEqual(command[command.index("-dev")+1], "1")
        command = self.engine._command(12345, prepared=[model, codec])
        self.assertEqual(command[command.index("-m")+1], str(model))
        self.assertEqual(command[command.index("--codec-model")+1], str(codec))
        self.assertNotIn("--auto-download", command)

    def test_cancel_terminates_inference_and_next_call_restarts(self):
        output = self.root / "cancelled.wav"
        self.engine.prepare()
        process = self.engine._process
        stopped = threading.Event()
        timer = threading.Timer(0.15, stopped.set)
        timer.start()
        start = time.monotonic()
        try:
            with self.assertRaisesRegex(RuntimeError, "cancelled"):
                self.engine.synth_wav("slow", output, stop_event=stopped)
        finally:
            timer.cancel();timer.join()
        self.assertLess(time.monotonic()-start, 2)
        self.assertIsNotNone(process.poll())
        self.assertFalse(output.exists())
        self.engine.synth_wav("new", output)
        self.assertNotEqual(self.engine._process.pid, process.pid)

    def test_http_errors_are_actionable_and_model_stays_loaded(self):
        with self.assertRaisesRegex(RuntimeError, "bad voice"):
            self.engine.synth_wav("error", self.root / "out.wav")
        process = self.engine._process
        self.engine.synth_wav("new", self.root / "out.wav")
        self.assertIs(self.engine._process, process)

    def test_factory_supports_piper_without_loading_ml_libraries(self):
        from vrm_overlay.speech import PiperTts
        provider = create_tts(SpeechConfig(provider="piper", model="piper", executable="piper", model_path="voice.onnx"))
        self.assertIsInstance(provider, PiperTts)
        self.assertEqual(provider.model_path, "voice.onnx")


class InstallerTests(unittest.TestCase):
    def archive(self, name="bin/crispasr"):
        blob = io.BytesIO()
        with tarfile.open(fileobj=blob, mode="w:gz") as tar:
            entry = tarfile.TarInfo(name);entry.size=6;entry.mode=0o755
            tar.addfile(entry, io.BytesIO(b"binary"))
        return blob.getvalue()

    def install(self, content, directory, digest=None):
        response = io.BytesIO(content)
        response.headers = {"Content-Length": str(len(content))}
        asset = ("test.tar.gz", digest or hashlib.sha256(content).hexdigest())
        with patch.dict(tts_install.ASSETS, {("Linux", "x86_64", "cpu"): asset}), \
             patch.object(tts_install.platform, "system", return_value="Linux"), \
             patch.object(tts_install, "_machine", return_value="x86_64"), \
             patch.object(tts_install, "urlopen", return_value=response):
            return tts_install.install_crispasr(device="cpu", directory=directory)

    def test_verified_install_keeps_runtime_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = self.install(self.archive(), directory)
            self.assertEqual(Path(binary).read_bytes(), b"binary")

    def test_bad_checksum_does_not_publish_install(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "checksum"):
                self.install(self.archive(), directory, "0"*64)
            self.assertFalse((Path(directory)/tts_install.VERSION/"cpu").exists())

    def test_archive_cannot_escape_install_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(tarfile.FilterError):
                self.install(self.archive("../../escaped"), directory)
            self.assertFalse((Path(directory)/tts_install.VERSION/"cpu").exists())
