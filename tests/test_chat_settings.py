import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

HAS_EXAMPLE_DEPS = importlib.util.find_spec("requests") is not None and importlib.util.find_spec("tkinter") is not None


@unittest.skipUnless(HAS_EXAMPLE_DEPS, "Optional chat example dependencies are not installed")
class ChatSettingsTests(unittest.TestCase):
    def test_new_settings_default_to_nano(self):
        from examples.chat_tts.chat_tts_overlay import Settings
        self.assertEqual(Settings().tts["provider"], "crispasr")
        self.assertEqual(Settings().tts["model"], "chatterbox-nano")
        self.assertEqual(Settings().tts["device"], "vulkan")

    def test_existing_piper_settings_migrate_without_losing_chat_options(self):
        from examples.chat_tts.chat_tts_overlay import Settings
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            path.write_text(json.dumps(dict(model="existing-chat-model", piper_bin="/bin/piper", piper_model="/voices/test.onnx")))
            settings = Settings.load(path)
            self.assertEqual(settings.model, "existing-chat-model")
            self.assertEqual(settings.tts["provider"], "piper")
            self.assertEqual(settings.tts["model_path"], "/voices/test.onnx")
            settings.save(path)
            self.assertEqual(Settings.load(path).tts, settings.tts)

    def test_legacy_plaintext_key_migrates_to_keyring_and_never_saves_back(self):
        from examples.chat_tts.chat_tts_overlay import Settings
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            path.write_text(json.dumps(dict(api_key="dummy-secret", model="existing-model")))
            with patch("examples.chat_tts.chat_tts_overlay.SystemCredentials") as store:
                settings = Settings.load(path)
                store.return_value.set.assert_called_once_with(settings.endpoint, "dummy-secret")
                self.assertEqual(settings.api_key, "dummy-secret")
                self.assertNotIn("dummy-secret", path.read_text())
                self.assertNotIn("api_key", json.loads(path.read_text()))
                self.assertNotIn("dummy-secret", repr(settings))

    def test_locked_keyring_leaves_legacy_file_intact(self):
        from examples.chat_tts.chat_tts_overlay import Settings
        from vrm_overlay.credentials import CredentialError
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            original = json.dumps(dict(api_key="dummy-secret", model="existing-model"))
            path.write_text(original)
            with patch("examples.chat_tts.chat_tts_overlay.SystemCredentials") as store:
                store.return_value.set.side_effect = CredentialError("Keyring locked")
                with self.assertRaises(CredentialError):
                    Settings.load(path)
            self.assertEqual(path.read_text(), original)
