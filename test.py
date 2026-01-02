#!/usr/bin/env python3
"""VRM Overlay Test Script"""

import json
import socket
import time
import sys

SOCKET_PATH = '/tmp/vrm-overlay.sock'

class VrmClient:
    def __init__(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(SOCKET_PATH)
        self.f = self.sock.makefile()
        self._id = 0

    def call(self, action, **params):
        self._id += 1
        req = {"id": self._id, "action": action, **params}
        self.sock.sendall((json.dumps(req) + "\n").encode())
        resp = json.loads(self.f.readline())
        return resp.get("status") == "ok", resp

    def close(self):
        self.sock.close()


def main():
    # Default paths - adjust as needed
    model_path = "/home/rab/code/vrm/test.vrm"
    anim_path = "/home/rab/code/vrm/VRMA_MotionPack/VRMA_01.vrma"

    if len(sys.argv) > 1:
        model_path = sys.argv[1]
    if len(sys.argv) > 2:
        anim_path = sys.argv[2]

    try:
        client = VrmClient()
    except FileNotFoundError:
        print(f"Error: Socket not found at {SOCKET_PATH}")
        print("Make sure vrm-overlay is running: ./vrm-overlay &")
        sys.exit(1)

    print("=== VRM Overlay Test ===\n")

    # Ping
    ok, resp = client.call("ping")
    print(f"ping: {'OK' if ok else 'FAIL'}")

    # Load model
    ok, resp = client.call("load", path=model_path)
    print(f"load model: {'OK' if ok else 'FAIL'} - {model_path}")

    # Load animation
    ok, resp = client.call("load_animation", path=anim_path)
    print(f"load animation: {'OK' if ok else 'FAIL'} - {anim_path}")

    # Set camera
    ok, resp = client.call("set_camera", azimuth=3.14, polar=1.5, dist=2.5, target=[0, 0.8, 0])
    print(f"set camera: {'OK' if ok else 'FAIL'}")

    # Play animation
    ok, resp = client.call("play", speed=1.0)
    print(f"play: {'OK' if ok else 'FAIL'}")

    print("\nAnimation playing for 10 seconds...")
    print("(Use mouse to orbit camera, scroll to zoom)")

    try:
        time.sleep(10)
    except KeyboardInterrupt:
        print("\nInterrupted")

    # Pause
    client.call("pause")
    print("\nPaused. Press Enter to quit or Ctrl+C to keep running...")

    try:
        input()
        client.call("quit")
    except KeyboardInterrupt:
        print("\nKeeping overlay running")

    client.close()


if __name__ == "__main__":
    main()
