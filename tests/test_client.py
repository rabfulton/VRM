import json
import socket
import threading
import time
import unittest

from vrm_overlay import IpcClient


class ClientTests(unittest.TestCase):
    def peer(self):
        client_sock, peer = socket.socketpair()
        client = IpcClient()
        client._sock = client_sock
        self.addCleanup(client.close)
        self.addCleanup(peer.close)
        return client, peer

    def test_fragmented_reply_and_unrelated_ack(self):
        client, peer = self.peer()
        def reply():
            request = json.loads(peer.recv(65536))
            peer.sendall(b'{"id":-1,"status":"ok"}\n{"id":')
            peer.sendall(str(request["id"]).encode() + b',"status":"ok","data":true}\n')
        thread = threading.Thread(target=reply)
        thread.start()
        self.assertTrue(client.request({"action": "ping"})["data"])
        thread.join(timeout=2)
        self.assertFalse(thread.is_alive())

    def test_timeout_closes_connection_without_replaying_command(self):
        client, peer = self.peer()
        with self.assertRaises(TimeoutError):
            client.request({"action": "side_effect"}, timeout_s=0.03)
        self.assertIsNone(client._sock)
        payload = peer.recv(65536)
        self.assertEqual(payload.count(b"\n"), 1)
        self.assertEqual(peer.recv(1), b"")

    def test_invalid_response_closes_transport(self):
        client, peer = self.peer()
        peer.sendall(b'not-json\n')
        with self.assertRaises(ValueError):
            client.request({"action": "ping"})
        self.assertIsNone(client._sock)

    def test_pcm_must_be_complete_samples(self):
        client = IpcClient()
        with self.assertRaises(ValueError):
            client.audio_stream_chunk(b"x")
