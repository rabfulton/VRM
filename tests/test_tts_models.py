import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from vrm_overlay.tts_models import ModelFile, download_model


class ModelDownloadTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.data = b"GGUFmodel-weights"
        self.asset = ModelFile("owner/model", "pinned-revision", "model.gguf", len(self.data),
                               hashlib.sha256(self.data).hexdigest())

    def response(self, body, status=200, headers=None):
        response = io.BytesIO(body)
        response.status = status
        response.headers = headers or {}
        return response

    def test_cancelled_partial_resumes_and_is_verified_before_publication(self):
        partial = self.root / "model.gguf.download"
        partial.write_bytes(self.data[:4])
        response = self.response(self.data[4:], 206, {"Content-Range": f"bytes 4-{len(self.data)-1}/{len(self.data)}"})
        with patch("vrm_overlay.tts_models.urlopen", return_value=response) as fetch:
            target = download_model(self.asset, self.root)
        request = fetch.call_args.args[0]
        self.assertEqual(request.headers["Range"], "bytes=4-")
        self.assertIn("pinned-revision", request.full_url)
        self.assertEqual(target.read_bytes(), self.data)
        self.assertFalse(partial.exists())

    def test_server_ignoring_range_restarts_instead_of_appending(self):
        (self.root / "model.gguf.download").write_bytes(b"old")
        with patch("vrm_overlay.tts_models.urlopen", return_value=self.response(self.data)):
            self.assertEqual(download_model(self.asset, self.root).read_bytes(), self.data)

    def test_bad_checksum_cannot_replace_existing_model(self):
        target = self.root / "model.gguf"
        target.write_bytes(b"previous-file")
        with patch("vrm_overlay.tts_models.urlopen", return_value=self.response(b"x"*len(self.data))):
            with self.assertRaisesRegex(RuntimeError, "checksum"):
                download_model(self.asset, self.root)
        self.assertEqual(target.read_bytes(), b"previous-file")

    def test_interrupted_download_keeps_partial_for_retry(self):
        with patch("vrm_overlay.tts_models.urlopen", return_value=self.response(self.data[:4])):
            with self.assertRaisesRegex(RuntimeError, "resume"):
                download_model(self.asset, self.root)
        self.assertEqual((self.root / "model.gguf.download").read_bytes(), self.data[:4])
        self.assertFalse((self.root / "model.gguf").exists())

    def test_verified_cache_needs_no_network(self):
        (self.root / "model.gguf").write_bytes(self.data)
        with patch("vrm_overlay.tts_models.urlopen") as fetch:
            self.assertEqual(download_model(self.asset, self.root).read_bytes(), self.data)
        fetch.assert_not_called()
