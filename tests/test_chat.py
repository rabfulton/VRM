"""Character protocol and credential guarantees, without Qt or external services."""
import json
import sys
import types
import unittest
from unittest.mock import patch

from vrm_overlay.chat import CharacterScript, CompletionStream, Sentences, StreamError, capabilities, sound_tags, system_prompt
from vrm_overlay.credentials import CredentialError, SystemCredentials, normalize_endpoint
from vrm_overlay.animation_library import describe_animations


def chunk(text, finish=None):
    return ("data: " + json.dumps({"choices": [{"index": 0, "delta": {"content": text}, "finish_reason": finish}]}, ensure_ascii=False) + "\r\n\r\n").encode()


class ChatProtocolTests(unittest.TestCase):
    def test_selected_animation_ids_handle_collisions_and_unsafe_characters(self):
        files = [dict(name=name, file=filename, path="/private/" + filename) for name, filename in (
            ("Relax", "Relax.vrma"), ("Clapping", "Clapping.vrma"),
            ("A B", "A B.vrma"), ("A-B", "A-B.glb"),
            ("[[animation:evil]]\nname", "[[animation:evil]] name.vrma"))]
        first = describe_animations(files)
        self.assertEqual([r["llm_enabled"] for r in first], [True, False, False, False, False])
        selected = describe_animations(files, {f["file"]: True for f in files})
        ids = [r["llm_id"] for r in selected]
        self.assertEqual(len(set(ids)), len(ids))
        for identifier in ids:
            self.assertRegex(identifier, r"^[a-z][a-z0-9_]*$")
        self.assertEqual(ids, [r["llm_id"] for r in first])
        available = capabilities({"model_loaded": True}, selected)
        self.assertEqual(len(available), len(files))
        prompt = system_prompt("Test", available, ())
        self.assertNotIn("/private", prompt)
        self.assertIn("[[animation:clapping]]", prompt)
        self.assertFalse(available[("animation", "clapping")].commands[1]["loop"])
        self.assertNotIn(("animation", "idle"), capabilities({"model_loaded": True}, describe_animations(files, {})))

    def test_sse_handles_every_byte_boundary_unicode_comments_and_usage(self):
        stream = CompletionStream()
        data = b": keepalive\r\n\r\n" + chunk("Hello, 世界! ") + chunk("[gasp]", "stop") + b'data: {"choices":[],"usage":{}}\r\n\r\ndata: [DONE]\r\n\r\n'
        output = []
        for byte in data:
            output.extend(stream.feed(bytes([byte])))
        output.extend(stream.feed(b"", final=True))
        self.assertEqual("".join(output), "Hello, 世界! [gasp]")
        self.assertTrue(stream.done)

    def test_truncated_bad_and_tool_call_streams_fail(self):
        for data in (chunk("unfinished"), b"data: not-json\n\n",
                     b'data: {"error":{"message":"private server detail"}}\n\n',
                     b'data: {"choices":[{"delta":{"tool_calls":[{}]}}]}\n\n',
                     chunk("Too long", "length")):
            with self.subTest(data=data), self.assertRaises(StreamError):
                CompletionStream().feed(data, final=True)

    def test_complete_finish_without_done_is_accepted_but_limits_are_bounded(self):
        parser = CompletionStream()
        self.assertEqual(parser.feed(chunk("Done.", "stop"), final=True), ["Done."])
        with self.assertRaisesRegex(StreamError, "response limit"):
            CompletionStream().feed(chunk("x" * 16001))
        with self.assertRaisesRegex(StreamError, "oversized"):
            CompletionStream().feed(b"x" * 65537)

    def test_cues_and_sound_tags_survive_all_fragmentation_without_becoming_speech(self):
        script = CharacterScript({("animation", "idle"), ("expression", "happy")}, ("gasp",))
        events = []
        for letter in "[[animation:idle]][gasp] Hello! [[expression:happy]]Nice to see you.":
            events.extend(script.feed(letter))
        events.extend(script.feed("", final=True))
        self.assertEqual([value for kind, value in events if kind == "action"], [("animation", "idle"), ("expression", "happy")])
        self.assertEqual([value for kind, value in events if kind == "say"], ["[gasp] Hello!", "Nice to see you."])
        self.assertNotIn("[[", "".join(value for kind, value in events if kind == "caption"))

    def test_code_unavailable_excessive_and_incomplete_cues_never_execute(self):
        script = CharacterScript({("expression", "happy")})
        data = "`[[expression:happy]]` ```\n[[expression:happy]]\n``` "
        data += "[[window:shake]][gasp] Hi. " + "[[expression:happy]]" * 12 + "[[expression:hap"
        events = []
        for letter in data:
            events.extend(script.feed(letter))
        events.extend(script.feed("", final=True))
        actions = [v for k, v in events if k == "action"]
        self.assertEqual(actions, [("expression", "happy")] * 8)
        spoken = " ".join(v for k, v in events if k == "say")
        self.assertEqual(spoken, "Hi.")
        self.assertNotIn("gasp", spoken)

    def test_sentences_wait_for_lookahead_handle_decimals_abbreviations_and_long_text(self):
        sentences = Sentences()
        self.assertEqual(sentences.feed("Ask Dr."), [])
        self.assertEqual(sentences.feed(" Smith about 3."), [])
        self.assertEqual(sentences.feed("14. Next"), ["Ask Dr. Smith about 3.14."])
        self.assertEqual(sentences.feed(" paragraph\nHello! "), ["Next paragraph", "Hello!"])
        result = sentences.feed("word " * 500, final=True)
        self.assertGreater(len(result), 1)
        self.assertTrue(all(len(s) <= 360 for s in result))
        self.assertEqual(" ".join(result), ("word " * 500).strip())

    def test_catalogue_is_small_model_specific_and_does_not_advertise_desktop_actions(self):
        state = {"model_loaded": True, "expressions": ["Joy", "aa", "Angry", "Blink"]}
        files = [dict(name=name, path="/private/" + name + ".vrma") for name in ("Relax", "Thinking", "Angry", "VRMA_02", "dance_fast", "crawling")]
        available = capabilities(state, files)
        self.assertEqual(len([c for c in available.values() if c.kind == "animation"]), 4)
        self.assertNotIn(("expression", "aa"), available)
        self.assertEqual(available[("expression", "happy")].commands[0]["name"], "Joy")
        prompt = system_prompt("A test character", available, ("gasp",))
        self.assertNotIn("/private", prompt)
        self.assertNotIn("dance_fast", prompt)
        self.assertIn("not available yet", prompt)
        self.assertIn("[gasp]", prompt)
        self.assertEqual(sound_tags({"provider": "piper", "model": "piper"}), ())
        self.assertEqual(capabilities({"model_loaded": False}, files), {})


class CredentialTests(unittest.TestCase):
    def test_endpoint_scope_and_cleartext_remote_urls(self):
        self.assertEqual(normalize_endpoint("https://EXAMPLE.com:443/v1/chat/completions/"), "https://example.com/v1/chat/completions")
        self.assertEqual(normalize_endpoint("http://[::1]:1234/v1/chat/completions"), "http://[::1]:1234/v1/chat/completions")
        for endpoint in ("http://example.com/v1", "https://user:key@example.com", "https://example.com?key=secret", "file:///tmp/anything", "https://example.com:bad"):
            with self.subTest(endpoint=endpoint), self.assertRaises(ValueError):
                normalize_endpoint(endpoint)
        self.assertNotEqual(SystemCredentials.account("https://one.example/v1"), SystemCredentials.account("https://two.example/v1"))

    def test_plaintext_and_chained_fallbacks_are_never_used(self):
        class FileBackend:
            priority = 100
        FileBackend.__module__ = "keyrings.alt.file"
        fake = types.SimpleNamespace(get_keyring=lambda: FileBackend())
        with patch.dict(sys.modules, keyring=fake), self.assertRaises(CredentialError):
            SystemCredentials._backend()
        class OSBackend:
            priority = 1
        OSBackend.__module__ = "keyring.backends.SecretService"
        backend = OSBackend()
        class Chain:
            backends = [FileBackend(), backend]
        Chain.__module__ = "keyring.backends.chainer"
        fake.get_keyring = lambda: Chain()
        with patch.dict(sys.modules, keyring=fake):
            self.assertIs(SystemCredentials._backend(), backend)

    def test_backend_error_does_not_echo_key_and_storage_requires_confirmation(self):
        class Backend:
            def set_password(self, *_args):
                raise RuntimeError("secret-key-in-error")
        with patch.object(SystemCredentials, "_backend", return_value=Backend()):
            with self.assertRaises(CredentialError) as error:
                SystemCredentials().set("https://example.com/v1", "secret-key-in-error")
            self.assertNotIn("secret-key-in-error", str(error.exception))
        class NotSaved:
            def set_password(self, *_args): pass
            def get_password(self, *_args): return None
        with patch.object(SystemCredentials, "_backend", return_value=NotSaved()), self.assertRaises(CredentialError):
            SystemCredentials().set("https://example.com/v1", "test-key")
