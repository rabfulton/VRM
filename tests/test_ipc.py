import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest

from vrm_overlay import IpcClient, IpcError

SERVER = Path(os.environ.get("VRM_TEST_IPC_SERVER", "build/tests/ipc-server")).resolve()


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="vrm-ipc-")
        self.addCleanup(self.directory.cleanup)
        self.path = str(Path(self.directory.name) / "runtime.sock")
        self.server = subprocess.Popen([str(SERVER), self.path], stdout=subprocess.DEVNULL)
        self.addCleanup(self.stop_server)
        deadline = time.monotonic() + 5
        while not Path(self.path).exists():
            if self.server.poll() is not None:
                self.fail(f"Test server exited: {self.server.returncode}")
            if time.monotonic() >= deadline:
                self.fail("Test server startup timed out")
            time.sleep(0.005)

    def stop_server(self):
        if self.server.poll() is None:
            self.server.terminate()
        self.assertEqual(self.server.wait(timeout=5), 0)

    def raw(self):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(3)
        sock.connect(self.path)
        self.addCleanup(sock.close)
        return sock

    def test_fragmented_and_coalesced_requests(self):
        sock = self.raw()
        with sock.makefile("rb") as reader:
            sock.sendall(b'{"action":"echo","id":1,')
            time.sleep(0.01)
            sock.sendall(b'"text":"hello"}\n{"action":"echo","id":2}\n')
            self.assertEqual(json.loads(reader.readline())["data"]["text"], "hello")
            self.assertEqual(json.loads(reader.readline())["id"], 2)

    def test_invalid_input_recovers_on_same_connection(self):
        sock = self.raw()
        with sock.makefile("rb") as reader:
            for payload in [b'{', b'[]', b'{"action":12}', b'{"action":null}',
                            b'{"action":"echo","id":1.5}', b'{"action":"echo","id":2147483648}',
                            b'{"action":"echo"}garbage', b'{"action":"echo"}\x00hidden']:
                sock.sendall(payload + b"\n")
                self.assertEqual(json.loads(reader.readline())["status"], "error")
            sock.sendall(b'{"action":"echo","id":7}\n')
            self.assertEqual(json.loads(reader.readline())["id"], 7)

    def test_large_reply_survives_partial_writes(self):
        sock = self.raw()
        sock.sendall(b'{"action":"large","id":3}\n')
        time.sleep(0.03)  # Let the small server send buffer fill before reading.
        with sock.makefile("rb") as reader:
            response = json.loads(reader.readline())
            self.assertEqual(response["id"], 3)
            self.assertEqual(response["data"], "x" * 400000)
            sock.sendall(b'{"action":"echo","id":4}\n')
            self.assertEqual(json.loads(reader.readline())["id"], 4)

    def test_oversized_line_disconnects_and_allows_reconnect(self):
        sock = self.raw()
        sock.sendall(b"x" * 65536)
        self.assertEqual(sock.recv(1), b"")
        with IpcClient(self.path) as client:
            self.assertEqual(client.request({"action": "echo"})["status"], "ok")

    def test_request_at_limit(self):
        sock = self.raw()
        prefix = b'{"action":"echo","text":"'
        suffix = b'","id":9}\n'
        sock.sendall(prefix + b"x" * (65536 - len(prefix) - len(suffix)) + suffix)
        with sock.makefile("rb") as reader:
            self.assertEqual(json.loads(reader.readline())["id"], 9)

    def test_slow_client_output_is_bounded(self):
        sock = self.raw()
        sock.sendall(b'{"action":"overflow"}\n')
        time.sleep(0.03)
        while sock.recv(65536):
            pass
        with IpcClient(self.path) as client:
            self.assertEqual(client.request({"action": "echo"})["status"], "ok")

    def test_disconnect_during_reply_does_not_kill_server(self):
        sock = self.raw()
        sock.sendall(b'{"action":"large"}\n')
        sock.close()
        time.sleep(0.02)
        with IpcClient(self.path) as client:
            self.assertEqual(client.request({"action": "echo"})["status"], "ok")

    def test_second_server_preserves_existing_socket(self):
        result = subprocess.run([str(SERVER), self.path], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 1)
        with IpcClient(self.path) as client:
            self.assertEqual(client.request({"action": "echo"})["status"], "ok")

    def test_python_helpers_drain_acks_and_serialize_threads(self):
        from concurrent.futures import ThreadPoolExecutor
        with IpcClient(self.path) as client:
            for i in range(20):
                client.send_only({"action": "echo", "n": i})
            with ThreadPoolExecutor(max_workers=4) as workers:
                results = list(workers.map(lambda i: client.request({"action": "echo", "n": i}), range(40)))
            self.assertEqual([r["data"]["n"] for r in results], list(range(40)))

    def test_python_server_error_keeps_connection_usable(self):
        with IpcClient(self.path) as client:
            with self.assertRaises(IpcError):
                client.request({"action": 23})
            self.assertEqual(client.request({"action": "echo"})["status"], "ok")

    def test_python_rejects_oversized_request_before_sending(self):
        with IpcClient(self.path) as client:
            with self.assertRaises(ValueError):
                client.request({"action": "echo", "data": "x" * 65536})
            self.assertEqual(client.request({"action": "echo"})["status"], "ok")

    def test_stale_socket_is_reclaimed(self):
        other_path = str(Path(self.directory.name) / "stale.sock")
        with socket.socket(socket.AF_UNIX) as stale:
            stale.bind(other_path)
        process = subprocess.Popen([str(SERVER), other_path], stdout=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 3
            while True:
                try:
                    with IpcClient(other_path) as client:
                        self.assertEqual(client.request({"action": "echo"})["status"], "ok")
                    break
                except ConnectionRefusedError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.01)
        finally:
            process.terminate()
            self.assertEqual(process.wait(timeout=5), 0)

    def test_regular_file_is_preserved(self):
        other_path = Path(self.directory.name) / "file.sock"
        other_path.write_text("keep me")
        process = subprocess.run([str(SERVER), str(other_path)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.assertEqual(process.returncode, 1)
        self.assertEqual(other_path.read_text(), "keep me")
