"""Optional Qt Quick tests. Socket/transport fixtures do not need a real display."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch
import base64
import struct
import threading
import wave

HAS_QT = importlib.util.find_spec("PySide6") is not None
if HAS_QT:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    os.environ["QT_QPA_PLATFORMTHEME"] = "basic"
    os.environ.setdefault("QT_QUICK_BACKEND", "software")
    from PySide6.QtCore import QObject, QPoint, QSettings, Qt, QUrl
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtNetwork import QLocalServer
    from PySide6.QtQml import QQmlApplicationEngine
    from PySide6.QtQuick import QQuickItem
    from PySide6.QtQuickControls2 import QQuickStyle
    from PySide6.QtTest import QTest
    from vrm_overlay.qt_controller import Controller, ROOT, initial_state


@unittest.skipUnless(HAS_QT, "Install requirements-gui.txt for Qt Quick checks")
class QtControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        QQuickStyle.setStyle("Fusion")
        cls.app = QGuiApplication.instance() or QGuiApplication([])

    def wait_for(self, predicate, timeout=3):
        deadline = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() > deadline:
                self.fail(f"Qt operation timed out; controller error: {self.bridge.lastError}")
            QTest.qWait(5)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="vrm-qt-")
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.files = [root / name for name in ("A.vrma", "B.glb", "C.vrma")]
        for path in self.files:
            path.touch()
        self.state = initial_state()
        self.state.update(api_version=1, model_loaded=True, model_path="avatar.vrm", expressions=["Joy", "Blink"],
                          blink=dict(enabled=True, rate=14, duration=150), speed=1, loop=True)
        self.state["foot_lock"]["supported"] = 2
        for side in ("left", "right"):
            self.state["foot_lock"][side]["supported"] = True
        self.commands = []
        self.fail_action = None
        self.peer = None
        self.buf = bytearray()
        self.audio_open = False
        self.audio_chunks = []
        self.block_audio = False
        self.server = QLocalServer()
        path = str(root / "runtime.sock")
        self.assertTrue(self.server.listen(path))
        self.addCleanup(self.server.close)
        self.server.newConnection.connect(self.accept_peer)
        self.bridge = Controller(path, attach=True, animation_dir=str(root),
                                 settings=QSettings(str(root / "settings.ini"), QSettings.IniFormat))
        self.addCleanup(self.bridge.shutdown)
        self.bridge.connectRuntime()
        self.wait_for(lambda: self.bridge.connected)

    def accept_peer(self):
        self.peer = self.server.nextPendingConnection()
        self.peer.readyRead.connect(self.read_commands)

    def read_commands(self):
        self.buf.extend(bytes(self.peer.readAll()))
        while b"\n" in self.buf:
            line, _, rest = self.buf.partition(b"\n")
            self.buf = bytearray(rest)
            command = json.loads(line)
            self.commands.append(command)
            action = command["action"]
            if action == self.fail_action:
                response = dict(id=command["id"], status="error", error=dict(message="deliberate failure"))
            else:
                if action == "load_animation":
                    self.state.update(animation_path=command["path"], animation_index=0, duration=2.0,
                                      clips=[dict(index=0, name="Clip", duration=2)])
                elif action == "play":
                    self.state["playing"] = True
                elif action == "pause":
                    self.state["playing"] = False
                elif action == "auto_blink_enable":
                    self.state["blink"]["enabled"] = command["enabled"]
                elif action == "set_foot_lock":
                    self.state["foot_lock"]["enabled"] = command["enabled"]
                elif action == "set_spring":
                    self.state["spring"].update({k: v for k, v in command.items() if k not in ("action", "id")})
                elif action == "audio_stream_begin":
                    self.audio_open = True
                elif action in ("audio_cancel", "audio_stream_end"):
                    self.audio_open = False
                elif action == "audio_stream_chunk":
                    self.audio_chunks.append(base64.b64decode(command["data"]))
                response = dict(id=command["id"], status="ok")
                if action == "get_state":
                    response["data"] = self.state
                elif action == "audio_status":
                    response["data"] = dict(active=True, available_seconds=0.8 if self.block_audio and self.audio_chunks else 0,
                                            drained=not self.audio_open)
            data = (json.dumps(response) + "\n").encode()
            # Split a frame across writes, as a real socket may do.
            self.peer.write(data[:9])
            self.peer.write(data[9:])

    def load_qml(self):
        self.engine = QQmlApplicationEngine()
        self.engine.rootContext().setContextProperty("bridge", self.bridge)
        self.engine.load(QUrl.fromLocalFile(str(ROOT / "vrm_overlay/qml/Main.qml")))
        self.assertTrue(self.engine.rootObjects())
        self.window = self.engine.rootObjects()[0]
        self.addCleanup(self.close_qml)
        QTest.qWait(30)

    def close_qml(self):
        self.window.close()
        self.engine.deleteLater()
        self.app.processEvents()

    def click(self, name):
        item = self.window.findChild(QQuickItem, name)
        self.assertIsNotNone(item, name)
        self.assertTrue(item.isEnabled(), name)
        point = item.mapToScene(item.boundingRect().center())
        QTest.mouseClick(self.window, Qt.LeftButton, Qt.NoModifier, QPoint(round(point.x()), round(point.y())))

    def test_next_previous_buttons_and_blink_switch(self):
        self.load_qml()
        self.assertFalse(any(c["action"] == "auto_blink_enable" for c in self.commands))
        self.click("nextAnimationButton")
        self.wait_for(lambda: self.bridge.selectedAnimation == 0 and not self.bridge.busy)
        self.click("nextAnimationButton")
        self.wait_for(lambda: self.bridge.selectedAnimation == 1 and not self.bridge.busy)
        self.click("previousAnimationButton")
        self.wait_for(lambda: self.bridge.selectedAnimation == 0 and not self.bridge.busy)
        self.click("autoBlinkSwitch")
        self.wait_for(lambda: not self.bridge.state["blink"]["enabled"])
        self.click("autoBlinkSwitch")
        self.wait_for(lambda: self.bridge.state["blink"]["enabled"])
        actions = [c["action"] for c in self.commands if c["action"] != "get_state"]
        self.assertEqual(actions[:9], ["load_animation", "blend_to", "play"] * 3)
        self.assertFalse(self.bridge.lastError)

    def test_foot_planting_control(self):
        self.load_qml()
        tabs = self.window.findChild(QQuickItem, "workspaceTabs")
        tabs.setProperty("currentIndex", 2)
        QTest.qWait(20)
        self.click("footLockSwitch")
        self.wait_for(lambda: not self.bridge.state["foot_lock"]["enabled"])
        self.click("footLockSwitch")
        self.wait_for(lambda: self.bridge.state["foot_lock"]["enabled"])

    def test_expression_selection_survives_state_polling(self):
        self.load_qml()
        selector = self.window.findChild(QQuickItem, "expressionSelector")
        self.assertIsNotNone(selector)
        selector.setProperty("currentIndex", 1)
        QTest.qWait(650)
        self.assertEqual(selector.property("currentIndex"), 1)

    def test_failed_load_aborts_blend_and_play(self):
        self.fail_action = "load_animation"
        self.bridge.playAnimation(1)
        self.wait_for(lambda: bool(self.bridge.lastError))
        self.assertFalse(any(c["action"] in ("blend_to", "play") for c in self.commands))
        self.assertIn("deliberate failure", self.bridge.lastError)

    def test_slider_changes_are_coalesced_and_flushed_before_save(self):
        for value in (1.1, 1.2, 1.3):
            self.bridge.tune("set_spring", "stiffness", value)
        self.bridge.command("save_tuning", {})
        self.wait_for(lambda: not self.bridge.busy)
        requests = [c for c in self.commands if c["action"] != "get_state"]
        self.assertEqual([c["action"] for c in requests], ["set_spring", "save_tuning"])
        self.assertEqual(requests[0]["stiffness"], 1.3)

    def test_wrap_and_poll_state_matches_selected_animation(self):
        self.bridge.playAnimation(0)
        self.wait_for(lambda: self.bridge.selectedAnimation == 0 and not self.bridge.busy)
        self.bridge.stepAnimation(-1)
        self.wait_for(lambda: self.bridge.selectedAnimation == 2 and not self.bridge.busy)
        self.assertEqual(self.bridge.state["animation_path"], str(self.files[2]))

    def test_disconnect_does_not_stop_attached_server(self):
        self.bridge.disconnectRuntime()
        QTest.qWait(20)
        self.assertFalse(self.bridge.connected)
        self.assertFalse(any(c["action"] == "shutdown" for c in self.commands))
        self.assertFalse(self.bridge.lastError)
        self.bridge.connectRuntime()
        self.wait_for(lambda: self.bridge.connected)

    def test_speech_profiles_survive_model_switch_and_controller_restart(self):
        self.load_qml()
        speech = self.bridge.speech
        self.assertEqual(speech.config["model"], "chatterbox-nano")
        self.assertEqual(speech.config["device"], "vulkan")
        self.assertEqual(self.window.findChild(QQuickItem, "ttsDeviceSelector").property("currentIndex"), 0)
        speech.setOption("model_path", "nano.gguf")
        speech.setOption("device", "cpu")
        speech.selectModel("chatterbox-turbo")
        speech.setOption("model_path", "turbo.gguf")
        speech.selectProvider("piper")
        speech.setOption("model_path", "voice.onnx")
        speech.selectProvider("crispasr")
        self.assertEqual(speech.config["model"], "chatterbox-turbo")
        speech.selectModel("chatterbox-nano")
        selector = self.window.findChild(QQuickItem, "ttsModelSelector")
        self.assertEqual(selector.property("currentIndex"), 0)
        other = Controller(self.bridge.socketPath, attach=True, settings=self.bridge._settings)
        self.addCleanup(other.shutdown)
        self.assertEqual(other.speech.config["model_path"], "nano.gguf")
        self.assertEqual(other.speech.config["device"], "cpu")

    def test_qml_speak_uses_existing_transport_and_drains_audio(self):
        class Synth:
            def synth_wav(self, text, output, stop_event=None):
                with wave.open(str(output), "wb") as wav:
                    wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                    wav.writeframes(struct.pack("<h", 900)*6000)
        with patch("vrm_overlay.qt_speech.create_tts", return_value=Synth()) as factory:
            self.load_qml()
            self.window.findChild(QQuickItem, "workspaceTabs").setProperty("currentIndex", 4)
            QTest.qWait(20)
            self.click("speakPreviewButton")
            self.wait_for(lambda: not self.bridge.speech.busy)
            self.assertFalse(self.bridge.speech.error)
            self.assertEqual(b"".join(self.audio_chunks), struct.pack("<h", 900)*6000)
            self.assertEqual(factory.call_args.args[0].model, "chatterbox-nano")
            self.assertEqual([c["action"] for c in self.commands].count("audio_stream_end"), 1)

    def test_switch_during_synthesis_discards_late_audio(self):
        started, release = threading.Event(), threading.Event()
        class Synth:
            def synth_wav(self, text, output, stop_event=None):
                started.set()
                release.wait(2)
                with wave.open(str(output), "wb") as wav:
                    wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                    wav.writeframes(bytes(4800))
        with patch("vrm_overlay.qt_speech.create_tts", return_value=Synth()):
            self.bridge.speech.speak("old")
            self.wait_for(started.is_set)
            self.bridge.speech.selectModel("chatterbox-turbo")
            release.set()
            QTest.qWait(100)
            self.assertFalse(self.audio_chunks)
            self.assertFalse(any(c["action"] == "audio_stream_begin" for c in self.commands))
            self.assertFalse(self.bridge.speech.busy)

    def test_audio_failure_ends_speech_without_leaving_it_busy(self):
        class Synth:
            def synth_wav(self, text, output, stop_event=None):
                with wave.open(str(output), "wb") as wav:
                    wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                    wav.writeframes(bytes(4800))
        self.fail_action = "audio_stream_chunk"
        with patch("vrm_overlay.qt_speech.create_tts", return_value=Synth()):
            self.bridge.speech.speak("hello")
            self.wait_for(lambda: bool(self.bridge.speech.error))
            self.assertFalse(self.bridge.speech.busy)
            self.assertIn("deliberate failure", self.bridge.speech.error)

    def test_switch_during_playback_cancels_audio_and_discards_pending_chunks(self):
        class Synth:
            def synth_wav(self, text, output, stop_event=None):
                with wave.open(str(output), "wb") as wav:
                    wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                    wav.writeframes(bytes(200000))
        self.block_audio = True
        with patch("vrm_overlay.qt_speech.create_tts", return_value=Synth()):
            self.bridge.speech.speak("old playback")
            self.wait_for(lambda: bool(self.audio_chunks))
            count = len(self.audio_chunks)
            self.bridge.speech.selectModel("chatterbox-turbo")
            self.wait_for(lambda: not self.audio_open)
            QTest.qWait(100)
            self.assertEqual(len(self.audio_chunks), count)
            self.assertFalse(self.bridge.speech.busy)
