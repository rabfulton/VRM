#!/usr/bin/env python3
"""VRM Overlay IPC Client - Test script"""

import json
import socket
import sys
import time

SOCKET_PATH = '/tmp/vrm-overlay.sock'

class VrmClient:
    def __init__(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(SOCKET_PATH)
        self.file = self.sock.makefile()
        self.id = 0

    def call(self, action, **kwargs):
        self.id += 1
        req = {"id": self.id, "action": action, **kwargs}
        self.sock.sendall((json.dumps(req) + "\n").encode())
        line = self.file.readline()
        return json.loads(line) if line else None

    def close(self):
        self.sock.close()

def main():
    try:
        client = VrmClient()
    except FileNotFoundError:
        print(f"Socket not found: {SOCKET_PATH}")
        print("Make sure vrm-overlay is running")
        sys.exit(1)

    # Test ping
    print("Testing ping...")
    print(f"  {client.call('ping')}")

    # Load model if provided
    if len(sys.argv) > 1:
        path = sys.argv[1]
        print(f"Loading: {path}")
        print(f"  {client.call('load', path=path)}")

        # Start animation
        print("Playing animation...")
        print(f"  {client.call('play', speed=1.0)}")

        # Let it play for a bit
        time.sleep(2)

        # Pause
        print("Pausing...")
        print(f"  {client.call('pause')}")

        # Set specific time
        print("Setting time to 0.5s...")
        print(f"  {client.call('set_time', t=0.5)}")

    # Camera test
    print("Setting camera...")
    print(f"  {client.call('set_camera', azimuth=0.5, polar=1.5, dist=2.5)}")

    client.close()
    print("Done!")

if __name__ == "__main__":
    main()
