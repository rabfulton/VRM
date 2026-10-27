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
                elif action == "load":
                    self.state.update(model_loaded=True, model_path=command["path"], animation_path="", duration=0, time=0, playing=False)
                elif action == "blend_to":
                    self.state.update(loop=command.get("loop",False), time=0)
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

    def click(self, name, window=None):
        window = window or self.window
        item = window.findChild(QQuickItem, name)
        if item is None:
            # ListView owns delegates through its model; follow visual parents
            # rather than assuming their QObject parent is the window.
            def find_visual(parent):
                if parent.objectName() == name:
                    return parent
                for child in parent.childItems():
                    found = find_visual(child)
                    if found is not None:
                        return found
                return None
            item = find_visual(window.contentItem())
        self.assertIsNotNone(item, name)
        self.assertTrue(item.isEnabled(), name)
        point = item.mapToScene(item.boundingRect().center())
        QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, QPoint(round(point.x()), round(point.y())))

    def test_chat_window_streams_to_real_speech_transport_and_qml_send_controls(self):
        from chat_http_fixture import ChatServer
        server = ChatServer(["[[expression:happy]][gasp] Hello! ", "Good to see you."])
        self.addCleanup(server.close)
        spoken = []
        class Synth:
            def synth_wav(self, text, output, stop_event=None):
                spoken.append(text)
                with wave.open(str(output), "wb") as wav:
                    wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                    wav.writeframes(bytes(4800))
        with patch("vrm_overlay.qt_speech.create_tts", return_value=Synth()):
            self.load_qml()
            chat = self.bridge.chat
            self.assertTrue(chat.saveSettings({**chat.config, "endpoint": server.endpoint, "model": "test-model", "no_key": True}))
            self.click("openChatButton")
            window = self.window.findChild(QObject, "chatWindow")
            self.assertTrue(window.isVisible())
            self.assertIsNone(window.transientParent())
            window.findChild(QQuickItem, "chatInput").setProperty("text", "Say hello")
            self.click("sendChatButton", window)
            self.wait_for(lambda: bool(server.requests) and not chat.busy)
            self.assertFalse(chat.error)
            self.assertEqual(spoken, ["[gasp] Hello!", "Good to see you."])
            self.assertEqual([c["action"] for c in self.commands].count("audio_stream_begin"), 2)
            self.assertEqual([c["action"] for c in self.commands].count("audio_stream_end"), 2)
            self.assertTrue(any(c["action"] == "set_expression" and c["name"] == "Joy" for c in self.commands))
            self.assertNotIn("[[", chat.messages[-1]["text"])
            window.grabWindow().save("/tmp/vrm-chat-preview.png")
            window.close()

    def test_chat_settings_dialog_is_usable_and_keeps_keys_out_of_settings(self):
        self.load_qml()
        self.click("openChatButton")
        window = self.window.findChild(QObject, "chatWindow")
        self.click("chatSettingsButton", window)
        QTest.qWait(30)
        dialog = window.findChild(QObject, "chatSettingsDialog")
        self.assertTrue(dialog.property("visible"))
        dialog.findChild(QQuickItem, "chatEndpoint").setProperty("text", "http://localhost:1234/v1/chat/completions")
        dialog.findChild(QQuickItem, "chatModel").setProperty("text", "local-test-model")
        dialog.findChild(QQuickItem, "chatNoKey").setProperty("checked", True)
        self.click("saveChatSettingsButton", window)
        self.wait_for(lambda: not dialog.property("visible"))
        self.assertEqual(self.bridge.chat.config["model"], "local-test-model")
        self.assertTrue(self.bridge.chat.config["no_key"])
        saved = self.bridge._settings.value("chat/config")
        self.assertNotIn("api_key", saved)
        window.close()

    def test_avatar_right_click_sequence_opens_chat_once_without_replaying_on_attach(self):
        requests = []
        self.bridge.chat.nearCursorRequested.connect(requests.append)
        self.state["chat_request_seq"] = 3
        self.bridge.refresh()
        self.wait_for(lambda: self.bridge._chat_request_seq == 3)
        self.assertEqual(requests, [])
        self.state["chat_request_seq"] = 4
        self.bridge.refresh()
        self.wait_for(lambda: bool(requests))
        self.bridge.refresh()
        self.wait_for(lambda: not self.bridge.busy)
        self.assertEqual(len(requests), 1)
        self.assertIn("left", requests[0])

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

    def test_animation_llm_checks_persist_without_playing_and_filter_uses_source_row(self):
        self.load_qml()
        self.assertEqual(self.bridge.llmAnimationCount, 0)
        self.click("llmAnimationCheck0")
        self.assertTrue(self.bridge.animationFiles[0]["llm_enabled"])
        self.assertIn("[[animation:a]]", self.bridge.chat.prompt)
        self.assertFalse(any(c["action"] == "load_animation" for c in self.commands))
        self.window.findChild(QQuickItem, "animationSearch").setProperty("text", "C")
        QTest.qWait(25)
        self.click("llmAnimationCheck2")
        self.assertTrue(self.bridge.animationFiles[2]["llm_enabled"])
        self.assertFalse(self.bridge.animationFiles[1]["llm_enabled"])
        other = Controller(self.bridge.socketPath, attach=True, settings=self.bridge._settings)
        self.addCleanup(other.shutdown)
        self.assertEqual(other.animationFolder, self.bridge.animationFolder)
        self.assertEqual([f["llm_enabled"] for f in other.animationFiles], [True, False, True])
        self.bridge.disconnectRuntime()
        QTest.qWait(20)
        self.click("llmAnimationCheck2")
        self.assertFalse(self.bridge.animationFiles[2]["llm_enabled"])

    def test_animation_permissions_are_per_library_and_new_files_are_not_auto_enabled(self):
        self.bridge.setAnimationLlmEnabled(1, True)
        root = Path(self.directory.name)
        folder = root / "another"
        folder.mkdir()
        (folder / "B.glb").touch()
        self.bridge.setAnimationFolder(str(folder))
        self.assertFalse(self.bridge.animationFiles[0]["llm_enabled"])
        self.bridge.setAnimationFolder(str(root))
        self.assertTrue(self.bridge.animationFiles[1]["llm_enabled"])
        (root / "Relax.vrma").touch()
        self.bridge.scanAnimations()
        relaxed = next(f for f in self.bridge.animationFiles if f["name"] == "Relax")
        self.assertFalse(relaxed["llm_enabled"])
        self.assertNotIn("[[animation:idle]]", self.bridge.chat.prompt)
        self.assertIn("[[animation:b]]", self.bridge.chat.prompt)

    def test_streamed_chat_can_call_a_checked_animation_but_cannot_call_unchecked_one(self):
        from chat_http_fixture import ChatServer
        server = ChatServer(["[[animation:a]][[animation:b]]Hello!"])
        self.addCleanup(server.close)
        self.bridge.setAnimationLlmEnabled(1, True)
        chat = self.bridge.chat
        self.assertTrue(chat.saveSettings({**chat.config, "endpoint": server.endpoint, "model": "test-model", "no_key": True, "read_aloud": False}))
        chat.send("Try a gesture")
        self.wait_for(lambda: bool(server.requests) and not chat.busy)
        self.assertFalse(chat.error)
        prompt = server.requests[0]["body"]["messages"][0]["content"]
        self.assertIn("[[animation:b]]", prompt)
        self.assertNotIn("[[animation:a]]", prompt)
        self.assertEqual([c["path"] for c in self.commands if c["action"] == "load_animation"], [str(self.files[1])])

    def test_unchecking_during_chat_revokes_pending_speech_and_animation(self):
        from chat_http_fixture import ChatServer
        server = ChatServer(["Hello! [[animation:b]]Later."])
        self.addCleanup(server.close)
        started, release = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        class Synth:
            def synth_wav(self, text, output, stop_event=None):
                started.set()
                release.wait(2)
                with wave.open(str(output), "wb") as wav:
                    wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                    wav.writeframes(bytes(4800))
        self.bridge.setAnimationLlmEnabled(1, True)
        chat = self.bridge.chat
        self.assertTrue(chat.saveSettings({**chat.config, "endpoint": server.endpoint, "model": "test-model", "no_key": True}))
        with patch("vrm_overlay.qt_speech.create_tts", return_value=Synth()):
            chat.send("Try a gesture")
            self.wait_for(started.is_set)
            self.bridge.setAnimationLlmEnabled(1, False)
            release.set()
            QTest.qWait(100)
            self.assertFalse(chat.busy)
            self.assertFalse(self.bridge.speech.busy)
            self.assertNotIn("[[animation:b]]", chat.prompt)
            self.assertFalse(any(c["action"] in ("load_animation", "audio_stream_chunk") for c in self.commands))

    def motion_library(self, idle_loop=True):
        from vrm_overlay.animation_config import save_properties
        root = Path(self.directory.name)
        entrance, idle = root / "Entrance.vrma", root / "Idle.vrma"
        for path, values in ((entrance, dict(loopable=False, startup=True, is_idle=False)),
                             (idle, dict(loopable=idle_loop, startup=False, is_idle=True))):
            path.touch()
            save_properties(path, values)
        self.bridge.scanAnimations()
        return entrance, idle

    def finish_current_motion(self):
        self.state["time"] = self.state["duration"]
        self.bridge.refresh()

    def test_startup_plays_once_then_enters_idle_and_manual_pause_cancels_ownership(self):
        entrance, idle = self.motion_library()
        avatar = Path(self.directory.name) / "new.vrm"
        avatar.touch()
        self.bridge.loadModel(str(avatar))
        self.wait_for(lambda: self.state["animation_path"] == str(entrance) and not self.bridge.busy)
        self.assertFalse(self.state["loop"])
        self.assertTrue(self.state["blink"]["enabled"])
        self.finish_current_motion()
        self.wait_for(lambda: self.state["animation_path"] == str(idle) and not self.bridge.busy)
        self.assertTrue(self.state["loop"])
        self.bridge.loadModel(str(avatar))
        self.wait_for(lambda: self.state["animation_path"] == str(entrance) and not self.bridge.busy)
        self.bridge.command("pause", {})
        self.wait_for(lambda: not self.state["playing"] and not self.bridge.busy)
        self.state.update(time=2, playing=True)
        self.bridge.refresh()
        QTest.qWait(80)
        self.assertEqual(self.state["animation_path"], str(entrance))
        self.assertIsNone(self.bridge._motion)

    def test_one_shot_idle_cycles_to_another_idle(self):
        from vrm_overlay.animation_config import save_properties
        entrance, idle = self.motion_library(idle_loop=False)
        second = Path(self.directory.name) / 'Rest.vrma'
        second.touch()
        save_properties(second,dict(loopable=True,startup=False,is_idle=True))
        self.bridge.scanAnimations()
        avatar=Path(self.directory.name)/'new.vrm'
        avatar.touch()
        self.bridge.loadModel(str(avatar))
        self.wait_for(lambda:self.state['animation_path']==str(entrance) and not self.bridge.busy)
        self.finish_current_motion()
        self.wait_for(lambda:self.state['animation_path']==str(idle) and not self.bridge.busy)
        self.assertFalse(self.state['loop'])
        self.finish_current_motion()
        self.wait_for(lambda:self.state['animation_path']==str(second) and not self.bridge.busy)
        self.assertTrue(self.state['loop'])

    def test_motion_owner_waits_for_post_play_state_before_matching_paths(self):
        with patch.object(self.bridge, 'refresh'):
            self.bridge.queueCharacterAnimation(self.bridge._animation_commands(str(self.files[1])), 'Character B')
            self.wait_for(lambda:not self.bridge.busy and self.bridge._motion is not None)
            self.assertNotEqual(self.bridge.state['animation_path'],str(self.files[1]))
            self.bridge._maybe_idle()
            self.assertIsNotNone(self.bridge._motion)
        self.bridge.refresh()
        self.wait_for(lambda:self.bridge.state['animation_path']==str(self.files[1]))
        self.assertIsNotNone(self.bridge._motion)

    def test_properties_editor_preserves_tuning_and_exposes_roles_to_prompt(self):
        path=self.files[1]
        path.with_suffix('.cfg').write_text('# Keep me\nspring.drag=1.250\nbounds.world_w=2.345\nloopable=0\n')
        self.bridge.scanAnimations()
        self.bridge.setAnimationLlmEnabled(1,True)
        self.load_qml()
        self.click('animationPropertiesButton1')
        QTest.qWait(25)
        dialog=self.window.findChild(QObject,'animationPropertiesDialog')
        self.assertTrue(dialog.property('visible'))
        dialog.findChild(QQuickItem,'animationLoopableProperty').setProperty('checked',True)
        dialog.findChild(QQuickItem,'animationStartupProperty').setProperty('checked',True)
        self.click('saveAnimationPropertiesButton')
        self.wait_for(lambda:not dialog.property('visible'))
        text=path.with_suffix('.cfg').read_text()
        self.assertIn('# Keep me',text)
        self.assertIn('spring.drag=1.250',text)
        self.assertIn('bounds.world_w=2.345',text)
        self.assertIn('startup=1',text)
        self.assertIn('loopable=1',text)
        self.assertIn('startup/entrance',self.bridge.chat.prompt)
        other=Controller(self.bridge.socketPath,attach=True,animation_dir=self.directory.name,settings=self.bridge._settings)
        self.addCleanup(other.shutdown)
        self.assertTrue(other.animationFiles[1]['loopable'])
        self.assertTrue(other.animationFiles[1]['startup'])

    def test_chat_one_shot_finishes_before_returning_to_an_unchecked_idle(self):
        from chat_http_fixture import ChatServer
        _entrance,idle=self.motion_library()
        self.bridge.setAnimationLlmEnabled(1,True)
        server=ChatServer(['[[animation:b]]Hello!'])
        self.addCleanup(server.close)
        chat=self.bridge.chat
        self.assertTrue(chat.saveSettings({**chat.config,'endpoint':server.endpoint,'model':'fixture','no_key':True,'read_aloud':False}))
        chat.send('Wave')
        self.wait_for(lambda:bool(server.requests) and not chat.busy and not self.bridge.busy)
        self.assertEqual(self.state['animation_path'],str(self.files[1]))
        self.assertFalse(self.state['loop'])
        self.finish_current_motion()
        self.wait_for(lambda:self.state['animation_path']==str(idle) and not self.bridge.busy)
        self.assertNotIn('[[animation:idle]]',server.requests[0]['body']['messages'][0]['content'])

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
