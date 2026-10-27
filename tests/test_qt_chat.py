"""Real HTTP/SSE and Qt character coordination, without credentials or ML models."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest

HAS_QT = importlib.util.find_spec("PySide6") is not None
if HAS_QT:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    os.environ["QT_QPA_PLATFORMTHEME"] = "basic"
    os.environ.setdefault("QT_QUICK_BACKEND", "software")
    from PySide6.QtCore import QObject, QSettings, QTimer, Signal
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtTest import QTest
    from vrm_overlay.qt_chat import ChatController
    from vrm_overlay.credentials import CredentialError
    from chat_http_fixture import ChatServer

    class Speech(QObject):
        changed = Signal()
        config = dict(provider="crispasr", model="chatterbox-nano")
        def __init__(self):
            super().__init__()
            self.busy, self.generation, self.error, self.status = False, 0, "", "Ready"
            self.spoken = []
            self.fail = False
        def speak(self, text):
            self.generation += 1
            self.busy, self.error, self.status = True, "", "Speaking"
            self.spoken.append(text)
            self.changed.emit()
            generation = self.generation
            def done():
                if generation != self.generation:
                    return
                self.busy = False
                if self.fail:
                    self.generation += 1
                    self.error = "Test speech failure"
                self.changed.emit()
            QTimer.singleShot(80, done)
        def stop(self):
            self.generation += 1
            self.busy = False
            self.changed.emit()

    class Runtime(QObject):
        stateChanged = Signal()
        libraryChanged = Signal()
        connected = True
        animationFolder = "/local/animations"
        state = dict(model_loaded=True, model_path="local-avatar.vrm", expressions=["Joy", "Angry"], blend=dict(duration=0.45))
        animationFiles = [dict(name="Relax", path="/local/Relax.vrma"), dict(name="Thinking", path="/local/Thinking.vrma")]
        def __init__(self):
            super().__init__()
            self.speech = Speech()
            self.commands = []
        def _queue(self, commands, _label, done=None, guard=None, **_kwargs):
            def run():
                if guard and not guard():
                    return
                self.commands.extend(commands)
                if done:
                    done({"status": "ok"})
            QTimer.singleShot(5, run)

    class Store:
        def __init__(self): self.values = {}
        def get(self, endpoint): return self.values.get(endpoint, "")
        def set(self, endpoint, value): self.values[endpoint] = value
        def delete(self, endpoint): self.values.pop(endpoint, None)


@unittest.skipUnless(HAS_QT, "Install requirements-gui.txt for Qt chat checks")
class QtChatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QGuiApplication.instance() or QGuiApplication([])

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="vrm-chat-test-")
        self.addCleanup(self.directory.cleanup)
        self.settings = QSettings(str(Path(self.directory.name) / "settings.ini"), QSettings.IniFormat)
        self.runtime = Runtime()
        self.store = Store()
        self.chat = ChatController(self.runtime, self.settings, credentials=self.store)
        self.addCleanup(self.chat.shutdown)

    def server(self, *args, **kwargs):
        server = ChatServer(*args, **kwargs)
        self.addCleanup(server.close)
        return server

    def wait(self, predicate, timeout=4):
        deadline = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() > deadline:
                self.fail("Chat timed out: " + self.chat.status + " / " + self.chat.error)
            QTest.qWait(5)

    def configure(self, endpoint, **values):
        self.assertTrue(self.chat.saveSettings({**self.chat.config, "endpoint": endpoint, "model": "fixture-model", "no_key": True, **values}), self.chat.error)

    def test_first_sentence_speaks_before_response_completes_and_actions_are_ordered(self):
        release = threading.Event()
        self.addCleanup(release.set)
        server = self.server(["[[expression:hap", "py]][gasp] Hello! ", ("[[animation:thinking]]How are you?", release)])
        self.configure(server.endpoint)
        self.chat.send("Hello")
        self.wait(lambda: bool(self.runtime.speech.spoken))
        self.assertTrue(self.chat.busy)
        self.assertEqual(self.runtime.speech.spoken, ["[gasp] Hello!"])
        self.assertEqual(self.runtime.commands[0]["action"], "set_expression")
        release.set()
        self.wait(lambda: not self.chat.busy)
        self.assertFalse(self.chat.error)
        self.assertEqual(self.runtime.speech.spoken, ["[gasp] Hello!", "How are you?"])
        self.assertEqual([c["action"] for c in self.runtime.commands], ["set_expression", "load_animation", "blend_to", "play"])
        self.assertNotIn("[[", self.chat.messages[-1]["text"])
        self.assertEqual(len(self.chat._history), 2)
        self.chat.send("One more question")
        self.wait(lambda: len(server.requests) == 2)
        self.assertEqual([m["role"] for m in server.requests[1]["body"]["messages"]], ["system", "user", "assistant", "user"])
        self.chat.stop()

    def test_stop_discards_late_network_speech_and_actions(self):
        release = threading.Event()
        self.addCleanup(release.set)
        server = self.server(["Hello! ", ("[[expression:angry]]Late sentence.", release)])
        self.configure(server.endpoint)
        self.chat.send("Hi")
        self.wait(lambda: bool(self.runtime.speech.spoken))
        self.chat.stop()
        count = len(self.runtime.commands)
        release.set()
        QTest.qWait(150)
        self.assertFalse(self.chat.busy)
        self.assertEqual(self.runtime.speech.spoken, ["Hello!"])
        self.assertEqual(len(self.runtime.commands), count)
        self.assertEqual(self.chat._history, [])

    def test_new_question_interrupts_old_turn_without_replaying_late_content(self):
        release = threading.Event()
        self.addCleanup(release.set)
        server = self.server(["Old first sentence. ", ("[[expression:angry]]Old late sentence.", release)])
        self.configure(server.endpoint)
        self.chat.send("Old question")
        self.wait(lambda: bool(self.runtime.speech.spoken))
        server.chunks = ["A new answer."]
        self.chat.send("New question")
        self.wait(lambda: len(server.requests) == 2 and not self.chat.busy)
        release.set()
        QTest.qWait(100)
        self.assertEqual(self.runtime.speech.spoken, ["Old first sentence.", "A new answer."])
        self.assertEqual(self.runtime.commands, [])
        self.assertEqual(self.chat._history[0]["content"], "New question")
        self.assertEqual(self.chat.messages[-1]["text"], "A new answer.")

    def test_profile_change_before_first_sentence_cancels_old_speech_contract(self):
        release = threading.Event()
        self.addCleanup(release.set)
        server = self.server([("[gasp] Old voice.", release)])
        self.configure(server.endpoint)
        self.chat.send("Hi")
        self.wait(lambda: bool(server.requests))
        self.runtime.speech.config = dict(provider="piper", model="piper")
        self.runtime.speech.changed.emit()
        self.wait(lambda: not self.chat.busy)
        release.set()
        QTest.qWait(80)
        self.assertEqual(self.runtime.speech.spoken, [])

    def test_credentials_are_endpoint_scoped_and_never_persisted_or_in_prompt(self):
        server = self.server()
        self.configure(server.endpoint, no_key=False, read_aloud=False)
        self.chat.saveKey("test-key-must-not-leak")
        self.wait(lambda: not self.chat.credentialBusy)
        self.chat.send("Hello")
        self.wait(lambda: not self.chat.busy)
        self.assertFalse(self.chat.error)
        request = server.requests[0]
        self.assertEqual(request["authorization"], "Bearer test-key-must-not-leak")
        self.assertNotIn("test-key-must-not-leak", json.dumps(request["body"]))
        self.settings.sync()
        self.assertNotIn("test-key-must-not-leak", Path(self.settings.fileName()).read_text())
        self.chat.removeKey()
        self.wait(lambda: not self.chat.credentialBusy)
        self.assertEqual(self.store.values, {})

    def test_cancel_during_keyring_unlock_cannot_start_a_request(self):
        release = threading.Event()
        self.addCleanup(release.set)
        server = self.server()
        self.configure(server.endpoint, no_key=False)
        self.store.get = lambda _endpoint: (release.wait(3), "test-key")[1]
        self.chat.send("Hi")
        self.wait(lambda: self.chat.credentialBusy)
        self.chat.stop()
        release.set()
        self.wait(lambda: not self.chat.credentialBusy)
        self.assertEqual(server.requests, [])

    def test_http_failure_never_echoes_server_body_and_redirect_is_not_followed(self):
        target = self.server()
        source = self.server(status=307, redirect=target.endpoint)
        self.configure(source.endpoint, read_aloud=False)
        self.chat.send("Hi")
        self.wait(lambda: not self.chat.busy)
        self.assertIn("HTTP 307", self.chat.error)
        self.assertNotIn("test-key-must-not-leak", self.chat.error)
        self.assertEqual(target.requests, [])

    def test_incomplete_response_and_speech_failure_have_distinct_outcomes(self):
        incomplete = self.server(["Partial response"], finish=False)
        self.configure(incomplete.endpoint, read_aloud=False)
        self.chat.send("Hi")
        self.wait(lambda: not self.chat.busy)
        self.assertTrue(self.chat.error)
        self.assertEqual(self.chat._history, [])
        server = self.server(["First sentence. Second sentence. Third sentence."])
        self.configure(server.endpoint, read_aloud=True)
        self.runtime.speech.fail = True
        self.chat.send("Hi again")
        self.wait(lambda: not self.chat.busy)
        self.assertIn("Speech unavailable", self.chat.error)
        self.assertIn("Third sentence.", self.chat.messages[-1]["text"])
        self.assertEqual(self.runtime.speech.spoken, ["First sentence."])
        self.assertEqual(len(self.chat._history), 2)

    def test_disabled_actions_and_code_cannot_execute_or_reach_speech(self):
        server = self.server(["```\n[[expression:angry]]\n```[[animation:idle]]Hi. "])
        self.configure(server.endpoint, actions=False)
        self.chat.send("Hi")
        self.wait(lambda: not self.chat.busy)
        self.assertEqual(self.runtime.commands, [])
        self.assertEqual(self.runtime.speech.spoken, ["Hi."])
        self.assertNotIn("[[animation:idle]]", server.requests[0]["body"]["messages"][0]["content"])
