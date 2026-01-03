#!/usr/bin/env python3
"""VRM Overlay Test - Press n/p to switch animations, q to quit"""

import json, socket, glob, os, sys, tty, termios, select, time

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/tmp/vrm-overlay.sock")

def cmd(c):
    sock.send((json.dumps(c) + "\n").encode())
    time.sleep(0.05)
    return sock.recv(4096)

cmd({"action": "load", "path": "/home/rab/code/vrm/test.vrm"})
cmd({"action": "set_visible", "visible": True})

anims = sorted(glob.glob("/home/rab/code/vrm/VRMA_MotionPack/*.vrma") +
               glob.glob("/home/rab/code/vrm/VRMA_MotionPack/*.glb"))
idx = 0

# Spring params (1.0 = use VRM file defaults, higher = more effect)
stiff, drag, grav = 1.0, 1.0, 1.0

def play(i):
    cmd({"action": "load_animation", "path": anims[i]})
    cmd({"action": "play"})
    print(f"[{i+1}/{len(anims)}] {os.path.basename(anims[i])}")

def show_spring():
    cmd({"action": "set_spring", "stiffness": stiff, "drag": drag, "gravity": grav})
    print(f"  Spring: stiff={stiff:.1f} drag={drag:.1f} grav={grav:.1f}")

old = termios.tcgetattr(sys.stdin)
try:
    tty.setcbreak(sys.stdin.fileno())
    print("n/p=anim, 1/2=stiff, 3/4=drag, 5/6=grav, r=reset, q=quit")
    show_spring()  # Apply defaults
    play(idx)
    while True:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            k = sys.stdin.read(1)
            if k in "qQ": break
            elif k in "nN ": idx = (idx + 1) % len(anims); play(idx)
            elif k in "pP": idx = (idx - 1) % len(anims); play(idx)
            elif k == '1': stiff = min(3.0, stiff + 0.1); show_spring()
            elif k == '2': stiff = max(0.0, stiff - 0.1); show_spring()
            elif k == '3': drag = min(3.0, drag + 0.1); show_spring()
            elif k == '4': drag = max(0.0, drag - 0.1); show_spring()
            elif k == '5': grav = min(5.0, grav + 0.2); show_spring()
            elif k == '6': grav = max(0.0, grav - 0.2); show_spring()
            elif k == 'r': cmd({"action": "reset_spring"}); print("  Springs reset")
    print(f"\n=== Final spring values ===")
    print(f"stiffness={stiff:.2f} drag={drag:.2f} gravity={grav:.2f}")
finally:
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
    sock.close()
