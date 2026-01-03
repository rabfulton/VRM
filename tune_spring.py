#!/usr/bin/env python3
"""Spring bone parameter tuner - keyboard controls"""
import json, socket, sys, tty, termios, select, time

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    sock.connect("/tmp/vrm-overlay.sock")
except:
    print("Error: vrm-overlay not running. Start it first.")
    sys.exit(1)

def cmd(c):
    sock.send((json.dumps(c) + "\n").encode())
    time.sleep(0.1)
    return sock.recv(4096)

# Load model if not loaded
cmd({"action": "load", "path": "/home/rab/code/vrm/test.vrm"})
time.sleep(0.5)

# Current values
stiffness = 1.0
drag = 1.0
gravity = 1.0

def send_params():
    cmd({"action": "set_spring", "stiffness": stiffness, "drag": drag, "gravity": gravity})
    print(f"Stiffness: {stiffness:.2f}  Drag: {drag:.2f}  Gravity: {gravity:.2f}")

old = termios.tcgetattr(sys.stdin)
try:
    tty.setcbreak(sys.stdin.fileno())
    print("Spring Bone Tuner")
    print("  q/a: stiffness +/-0.1")
    print("  w/s: drag +/-0.1")
    print("  e/d: gravity +/-0.2")
    print("  r: reset springs")
    print("  ESC: quit")
    print()
    send_params()
    
    while True:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            k = sys.stdin.read(1)
            if k == '\x1b': break
            elif k == 'q': stiffness = min(3.0, stiffness + 0.1)
            elif k == 'a': stiffness = max(0.0, stiffness - 0.1)
            elif k == 'w': drag = min(3.0, drag + 0.1)
            elif k == 's': drag = max(0.0, drag - 0.1)
            elif k == 'e': gravity = min(5.0, gravity + 0.2)
            elif k == 'd': gravity = max(0.0, gravity - 0.2)
            elif k == 'r': cmd({"action": "reset_spring"}); print("Springs reset")
            else: continue
            send_params()
    
    print(f"\n=== Final values ===")
    print(f"g_spring_stiffness = {stiffness:.2f}f;")
    print(f"g_spring_drag = {drag:.2f}f;")
    print(f"g_spring_gravity = {gravity:.2f}f;")
finally:
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
    sock.close()
