"""Persistent preview cache using a local HTTP fixture, without external requests."""
import importlib.util
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest

HAS_QT = importlib.util.find_spec('PySide6') is not None
if HAS_QT:
    os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
    from PySide6.QtCore import QBuffer, QIODevice
    from PySide6.QtGui import QGuiApplication, QImage
    from PySide6.QtTest import QTest
    from vrm_overlay.qt_avatar_library import AvatarLibrary
    from vrm_overlay.qt_thumbnail_cache import ThumbnailCache


@unittest.skipUnless(HAS_QT, 'Install requirements-gui.txt for thumbnail checks')
class ThumbnailCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QGuiApplication.instance() or QGuiApplication([])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        image = QImage(960, 640, QImage.Format_RGB32)
        image.fill(0xff52cbb4)
        buffer = QBuffer()
        buffer.open(QIODevice.WriteOnly)
        self.assertTrue(image.save(buffer, 'PNG'))
        self.payload = bytes(buffer.data())
        self.status = 200
        self.requests = 0
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_GET(self):
                fixture.requests += 1
                self.send_response(fixture.status)
                self.send_header('Content-Length', str(len(fixture.payload)))
                self.end_headers()
                try:
                    self.wfile.write(fixture.payload)
                except (BrokenPipeError, ConnectionResetError):
                    pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.close_server)
        self.url = f'http://127.0.0.1:{self.server.server_port}/thumbnail.png'

    def close_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=1)

    def wait_for(self, predicate):
        deadline = time.monotonic() + 3
        while not predicate():
            if time.monotonic() > deadline:
                self.fail('Thumbnail operation timed out')
            QTest.qWait(5)

    def test_gallery_cache_survives_restart_and_works_offline_without_refetch(self):
        library = AvatarLibrary(root=self.root / 'avatars')
        self.addCleanup(library.shutdown)
        library._featured = [dict(id='fixture', thumbnail=self.url)]
        self.assertEqual(library.featured[0]['thumbnail'], '')
        updates = []
        library.featuredChanged.connect(lambda: updates.append(True))
        library.refresh()
        library.refresh()  # A second opening during a fetch must not duplicate it.
        self.wait_for(lambda: bool(library.featured[0]['thumbnail']))
        self.assertTrue(updates)
        self.assertEqual(self.requests, 1)
        path = next((self.root / 'avatars/.thumbnails').glob('*.png'))
        cached = QImage(str(path))
        self.assertEqual((cached.width(), cached.height()), (480, 320))
        library.shutdown()
        self.close_server()
        reopened = AvatarLibrary(root=self.root / 'avatars')
        self.addCleanup(reopened.shutdown)
        reopened._featured = [dict(id='fixture', thumbnail=self.url)]
        self.assertEqual(reopened.featured[0]['thumbnail'], path.as_uri())
        reopened.refresh()
        QTest.qWait(50)
        self.assertFalse(reopened._thumbnails._pending)
        self.assertEqual(self.requests, 1)

    def test_invalid_or_failed_responses_are_not_cached_and_can_retry(self):
        cache = ThumbnailCache(self.root / 'cache')
        self.addCleanup(cache.shutdown)
        valid = self.payload
        for payload, status in ((b'not an image', 200), (valid, 404)):
            self.payload, self.status = payload, status
            cache.ensure([self.url])
            self.wait_for(lambda: not cache._pending)
            self.assertEqual(cache.localUrl(self.url), '')
        self.payload, self.status = valid, 200
        cache.ensure([self.url])
        self.wait_for(lambda: bool(cache.localUrl(self.url)))
        self.assertEqual(self.requests, 3)
        # A damaged on-disk image is fetched again rather than kept forever.
        cache._path(self.url).write_bytes(b'broken cache')
        cache.ensure([self.url])
        self.wait_for(lambda: bool(cache.localUrl(self.url)))
        self.assertEqual(self.requests, 4)

    def test_oversized_response_is_aborted_without_an_install(self):
        cache = ThumbnailCache(self.root / 'cache')
        self.addCleanup(cache.shutdown)
        cache.MAX_BYTES = 32
        cache.ensure([self.url])
        self.wait_for(lambda: not cache._pending)
        self.assertEqual(cache.localUrl(self.url), '')
        self.assertFalse(list(cache.root.glob('*')))

    def test_shutdown_aborts_pending_requests_without_publishing(self):
        cache = ThumbnailCache(self.root / 'cache')
        changes = []
        cache.changed.connect(lambda: changes.append(True))
        cache.ensure([self.url])
        cache.shutdown()
        QTest.qWait(30)
        self.assertFalse(cache._pending)
        self.assertFalse(changes)
        self.assertFalse(list(cache.root.glob('*')))


if __name__ == '__main__':
    unittest.main()
