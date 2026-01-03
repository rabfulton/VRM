#!/usr/bin/env python3
"""VRM Overlay Test - Press n/p to switch animations, q to quit"""

import json, socket, glob, os, sys, tty, termios, select

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/tmp/vrm-overlay.sock")

def cmd(c):
    sock.send((json.dumps(c) + "\n").encode())
    return sock.recv(4096)

cmd({"action": "load", "path": "/home/rab/code/vrm/test.vrm"})
cmd({"action": "set_visible", "visible": True})

anims = sorted(glob.glob("/home/rab/code/vrm/VRMA_MotionPack/VRMA_0[1-7].vrma") +
               glob.glob("/home/rab/code/vrm/VRMA_MotionPack/*.glb"))
idx = 0

def play(i):
    cmd({"action": "load_animation", "path": anims[i]})
    cmd({"action": "play"})
    print(f"[{i+1}/{len(anims)}] {os.path.basename(anims[i])}")

old = termios.tcgetattr(sys.stdin)
try:
    tty.setcbreak(sys.stdin.fileno())
    print("n=next, p=prev, q=quit")
    play(idx)
    while True:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            k = sys.stdin.read(1)
            if k in "qQ": break
            elif k in "nN ": idx = (idx + 1) % len(anims); play(idx)
            elif k in "pP": idx = (idx - 1) % len(anims); play(idx)
finally:
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
    sock.close()
