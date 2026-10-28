"""Presentation ownership, local controls, revocation and private companion transport."""
from dataclasses import asdict
import importlib.util
import os
from pathlib import Path
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HAS_QT = importlib.util.find_spec('PySide6') is not None
if HAS_QT:
    os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
    os.environ.setdefault('QT_QUICK_BACKEND', 'software')
    from PySide6.QtCore import QObject
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtTest import QTest
    from vrm_overlay.companion import CompanionClient
    from vrm_overlay.presentation import PresentationQueue
    from vrm_overlay.qt_companion import CompanionServer
    from vrm_overlay.tts import SpeechConfig


class FakeSpeech:
    def __init__(self):
        self.config = asdict(SpeechConfig())
        self.generation = 0
        self.busy = False
        self.error = ''
        self.utterances = []

    def speak(self, text, *, config=None):
        self.generation += 1
        self.busy = True
        self.utterances.append(text)

    def stop(self):
        self.generation += 1
        self.busy = False


if HAS_QT:
    class FakeRuntime(QObject):
        def __init__(self):
            super().__init__()
            self.connected = self.externalAgent = True
            self.busy = self._closing = False
            self.state = dict(model_loaded=True, model_path='avatar.vrm', expressions=['Joy'],
                              blend=dict(duration=.45), speed=1, animation_path='', playing=False)
            self._files = [dict(name='Wave', file='Wave.vrma', path='/library/Wave.vrma', enabled=True,
                                llm_enabled=True, loopable=True, startup=False, is_idle=False)]
            self.animationFolder = '/library'
            self.speech = FakeSpeech()
            self.chat = SimpleNamespace(busy=False)
            self._motion_generation = self._state_revision = 0
            self._motion = None
            self.commands = []
            self.presentations = PresentationQueue(self)

        def _queue(self, commands, label, done=None, failed=None, guard=None):
            if guard is None or guard():
                self.commands.extend(commands)
                if done:
                    done(dict(status='ok'))

        def queueCharacterAnimation(self, commands, label, **kwargs):
            self._motion_generation += 1
            self._motion = dict(mode='action', path=commands[0]['path'])
            self.state.update(animation_path=commands[0]['path'], playing=True)
            self._queue(commands, label, **kwargs)

        def _invalidate_motion(self):
            self._motion_generation += 1
            self._motion = None

        def resumeIdle(self):
            self._motion = dict(mode='idle')


@unittest.skipUnless(HAS_QT, 'PySide6 is required')
class PresentationFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QGuiApplication.instance() or QGuiApplication([])

    def setUp(self):
        self.runtime = FakeRuntime()
        self.manager = self.runtime.presentations
        prepared = patch('vrm_overlay.presentation.speech_configuration', side_effect=lambda config: config.copy())
        prepared.start()
        self.addCleanup(prepared.stop)
        self.addCleanup(self.cleanup)

    def cleanup(self):
        self.runtime._closing = True
        self.manager.cancel_all()
        self.manager.timer.stop()

    def threaded(self, operation, timeout=8):
        result, errors = [], []
        def work():
            try:
                result.append(operation())
            except BaseException as error:
                errors.append(error)
        thread = threading.Thread(target=work, daemon=True)
        thread.start()
        deadline = time.monotonic() + timeout
        while thread.is_alive() and time.monotonic() < deadline:
            QTest.qWait(5)
        self.assertFalse(thread.is_alive(), 'Operation timed out')
        if errors:
            raise errors[0]
        return result[0]


class PresentationTests(PresentationFixture):
    def test_speech_is_queued_and_completion_waits_for_drain(self):
        first = self.manager.submit('agent', text='First.')['id']
        second = self.manager.submit('agent', text='Second.')['id']
        self.manager.pump()
        self.assertEqual(self.runtime.speech.utterances, ['First.'])
        self.manager.pump()
        self.assertEqual(self.manager.status('agent', first)['state'], 'presenting')
        self.runtime.speech.busy = False
        self.manager.pump()
        self.assertEqual(self.manager.status('agent', first)['state'], 'completed')
        self.manager.pump()
        self.assertEqual(self.runtime.speech.utterances, ['First.', 'Second.'])
        self.assertEqual(self.manager.status('agent', second)['state'], 'presenting')

    def test_cancel_is_scoped_and_does_not_stop_another_speech_owner(self):
        identifier = self.manager.submit('one', text='Hello.')['id']
        self.manager.pump()
        with self.assertRaises(ValueError):
            self.manager.cancel('two', identifier)
        self.runtime.speech.speak('Manual preview.')
        generation = self.runtime.speech.generation
        self.manager.cancel('one', identifier)
        self.assertTrue(self.runtime.speech.busy)
        self.assertEqual(self.runtime.speech.generation, generation)

    def test_permission_revocation_and_mode_change_invalidate_queued_work(self):
        gesture = self.manager.capabilities()['gestures'][0]['id']
        identifier = self.manager.submit('one', gesture=gesture)['id']
        self.runtime._files[0]['llm_enabled'] = False
        self.manager.pump()
        self.assertEqual(self.manager.status('one', identifier)['state'], 'cancelled')
        self.assertFalse(self.runtime.commands)
        self.runtime.externalAgent = False
        with self.assertRaisesRegex(ValueError, 'External agent'):
            self.manager.submit('one', text='No.')

    def test_gesture_is_one_pass_and_waits_for_fresh_finished_state(self):
        gesture = self.manager.capabilities()['gestures'][0]['id']
        identifier = self.manager.submit('one', gesture=gesture)['id']
        self.manager.pump()
        self.assertFalse(next(c for c in self.runtime.commands if c['action'] == 'blend_to')['loop'])
        self.manager.pump()
        self.assertEqual(self.manager.status('one', identifier)['state'], 'presenting')
        self.runtime._state_revision += 1
        self.runtime.state['playing'] = False
        self.manager.pump()
        self.assertEqual(self.manager.status('one', identifier)['state'], 'completed')

    def test_unprepared_speech_and_unknown_actions_are_rejected(self):
        with patch('vrm_overlay.presentation.speech_configuration', return_value=None):
            with self.assertRaisesRegex(ValueError, 'Prepare'):
                self.manager.submit('one', text='Hello.')
        with self.assertRaisesRegex(ValueError, 'Unavailable'):
            self.manager.submit('one', gesture='../../private.vrma')

    def test_cancelled_queued_work_releases_capacity(self):
        self.runtime.speech.busy = True
        for _ in range(32):
            identifier = self.manager.submit('one', text='Queued.')['id']
            self.manager.cancel('one', identifier)
        self.assertFalse(self.manager.pending)

    def test_private_server_sessions_do_not_share_presentation_authority(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / 'companion.sock')
            server = CompanionServer(self.runtime, path)
            one, two = CompanionClient(path), CompanionClient(path)
            try:
                self.assertEqual(Path(path).stat().st_mode & 0o777, 0o600)
                identifier = self.threaded(lambda: one.call('avatar_present', dict(text='Hello.')))['id']
                with self.assertRaisesRegex(ValueError, 'not found'):
                    self.threaded(lambda: two.call('avatar_cancel', dict(presentation_id=identifier)))
                self.threaded(lambda: two.call('avatar_capabilities', {}))
                one.close()
                QTest.qWait(80)
                self.assertEqual(self.manager.jobs[identifier]['state'], 'cancelled')
                with self.assertRaisesRegex(RuntimeError, 'Another companion'):
                    CompanionServer(self.runtime, path)
            finally:
                one.close()
                two.close()
                QTest.qWait(10)
                server.close()
