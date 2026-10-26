import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

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
