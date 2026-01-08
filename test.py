#!/usr/bin/env python3
"""VRM Overlay Test - Interactive animation and spring bone testing

Controls:
  n/p or space  - Next/previous animation
  b             - Blend to next animation (crossfade)
  [/]           - Adjust blend duration (-/+ 0.1s)
  x/y/z         - Set axis correction (rotX/rotY/rotZ)
  a             - Axis correction auto
  v             - Axis correction none
  7/8           - Adjust spring stiffness
  9/0           - Adjust spring drag  
  -/=           - Adjust spring gravity
  r             - Reset spring bones
  e             - List available expressions
  1-5           - Set expression (A/I/U/E/O) with current weight
  w/s           - Increase/decrease expression weight
  c             - Clear all expression weights to 0
  q             - Quit
"""

import json, socket, glob, os, sys, tty, termios, select, time, subprocess

ROOT = os.path.dirname(os.path.abspath(__file__))
SOCK_PATH = "/tmp/vrm-overlay.sock"
OVERLAY_BIN = os.path.join(ROOT, "vrm-overlay")

overlay_proc = subprocess.Popen([OVERLAY_BIN], cwd=ROOT)

def connect_ipc(path, timeout=10.0):
    deadline = time.time() + timeout
    last_err = None
    while time.time() < deadline:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.connect(path)
            return s
        except (FileNotFoundError, ConnectionRefusedError, OSError) as e:
            last_err = e
            s.close()
            time.sleep(0.1)
    raise RuntimeError(f"Failed to connect to IPC socket {path}: {last_err}")

try:
    sock = connect_ipc(SOCK_PATH)
except Exception:
    overlay_proc.terminate()
    raise

def cmd(c):
    sock.send((json.dumps(c) + "\n").encode())
    time.sleep(0.05)
    return sock.recv(4096)

cmd({"action": "load", "path": "/home/rab/code/vrm/test1.vrm"})
cmd({"action": "set_visible", "visible": True})

anims = sorted(glob.glob("/home/rab/code/vrm/animation/*.vrma") +
               glob.glob("/home/rab/code/vrm/animation/*.glb"))
idx = 0

# Spring params (1.0 = use VRM file defaults, higher = more effect)
stiff, drag, grav = 1.0, 1.0, 1.0

# Blend params
blend_dur = "auto"

# Axis correction mode
axis_mode = 3  # Default rotY 180
axis_names = ["none", "auto", "rotX 180", "rotY 180", "rotZ 180"]

# Expression params
expr_weight = 1.0
expr_vowels = ["a", "i", "u", "e", "o"]  # VRM preset names (single letters)

def play(i, blend=False):
    cmd({"action": "load_animation", "path": anims[i]})
    if blend:
        cmd({"action": "blend_to", "index": 0, "duration": blend_dur, "loop": True})
    else:
        cmd({"action": "blend_to", "index": 0, "duration": 1.5, "loop": True})
    cmd({"action": "play"})
    print(f"[{i+1}/{len(anims)}] {os.path.basename(anims[i])}" + (" (blended)" if blend else ""))

def show_spring():
    cmd({"action": "set_spring", "stiffness": stiff, "drag": drag, "gravity": grav})
    print(f"  Spring: stiff={stiff:.1f} drag={drag:.1f} grav={grav:.1f}")

def set_axis(mode):
    global axis_mode
    axis_mode = mode
    cmd({"action": "set_anim_correction", "mode": mode})
    print(f"  Axis correction: {axis_names[mode]}")

def list_expressions():
    """List all available expressions for the loaded model."""
    resp = cmd({"action": "list_expressions"})
    try:
        data = json.loads(resp.decode())
        if "expressions" in data:
            names = data["expressions"]
            print(f"  Available expressions ({len(names)}):")
            for i, name in enumerate(names):
                print(f"    {i+1:2}. {name}")
        else:
            print(f"  Response: {data}")
    except json.JSONDecodeError:
        print(f"  Raw response: {resp}")

def set_expression(name, weight):
    """Set expression weight by name."""
    cmd({"action": "set_expression", "name": name, "weight": weight})
    print(f"  Expression '{name}' = {weight:.2f}")

def clear_expressions():
    """Clear all expression weights to 0."""
    for vowel in expr_vowels:
        cmd({"action": "set_expression", "name": vowel, "weight": 0.0})
    print("  Cleared all expression weights")

old = termios.tcgetattr(sys.stdin)
try:
    tty.setcbreak(sys.stdin.fileno())
    print("p=anim, b=blend, [/]=dur, x/y/z=axis, 7-0=spring, r=reset, q=quit")
    print("x/y/z = Set axis correction (rotX/rotY/rotZ), a=auto, v=none")
    print("7/8 - Adjust spring stiffness")
    print("9/0 - Adjust spring drag")
    print("-/= - Adjust spring gravity")
    print("e=list expressions, 1-5=set vowel (A/I/U/E/O), w/s=weight, c=clear")
    show_spring()
    play(idx)
    while True:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            k = sys.stdin.read(1)
            if k in "qQ": break
            elif k in "nN ": idx = (idx + 1) % len(anims); play(idx)
            elif k in "pP": idx = (idx - 1) % len(anims); play(idx)
            elif k in "bB": idx = (idx + 1) % len(anims); play(idx, blend=True)
            elif k == '[': blend_dur = max(0.05, blend_dur - 0.1);
            elif k == ']': blend_dur = min(10.0, blend_dur + 0.1);
            # Axis correction keys
            elif k == 'x': set_axis(2)
            elif k == 'y': set_axis(3)
            elif k == 'z': set_axis(4)
            elif k == 'a': set_axis(1)
            elif k == 'v': set_axis(0)
            # Spring controls
            elif k == '7': stiff = min(3.0, stiff + 0.1); show_spring()
            elif k == '8': stiff = max(0.0, stiff - 0.1); show_spring()
            elif k == '9': drag = min(3.0, drag + 0.1); show_spring()
            elif k == '0': drag = max(0.0, drag - 0.1); show_spring()
            elif k == '-': grav = max(0.0, grav - 0.2); show_spring()
            elif k == '=': grav = min(5.0, grav + 0.2); show_spring()
            elif k == 'r': cmd({"action": "reset_spring"}); print("  Springs reset")
            # Expression controls
            elif k == 'e': list_expressions()
            elif k == '1': set_expression("a", expr_weight)
            elif k == '2': set_expression("i", expr_weight)
            elif k == '3': set_expression("u", expr_weight)
            elif k == '4': set_expression("e", expr_weight)
            elif k == '5': set_expression("o", expr_weight)
            elif k == 'w': expr_weight = min(1.0, expr_weight + 0.1); print(f"  Expression weight: {expr_weight:.2f}")
            elif k == 's': expr_weight = max(0.0, expr_weight - 0.1); print(f"  Expression weight: {expr_weight:.2f}")
            elif k == 'c': clear_expressions()
    print(f"\n=== Final values ===")
    print(f"spring: stiffness={stiff:.2f} drag={drag:.2f} gravity={grav:.2f}")
    print(f"blend: duration={blend_dur:.2f}s" if isinstance(blend_dur, float) else f"blend: duration={blend_dur}")
    print(f"axis: {axis_names[axis_mode]}")
finally:
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
    try:
        cmd({"action": "shutdown"})
    except Exception:
        pass
    try:
        sock.close()
    except Exception:
        pass
    if overlay_proc.poll() is None:
        try:
            overlay_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            overlay_proc.terminate()

