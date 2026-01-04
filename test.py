#!/usr/bin/env python3
"""VRM Overlay Test - Interactive animation and spring bone testing

Controls:
  n/p or space  - Next/previous animation
  b             - Blend to next animation (crossfade)
  l             - Toggle loop blending on/off
  s             - Toggle smooth loop (skip spring reset on loop)
  [/]           - Adjust blend duration (-/+ 0.1s)
  0-5           - Set axis correction (0=none, 1=auto, 2=rotX, 3=rotY, 4=rotZ)
  7/8           - Adjust spring stiffness
  9/0           - Adjust spring drag  
  -/=           - Adjust spring gravity
  r             - Reset spring bones
  q             - Quit
"""

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
               glob.glob("/home/rab/code/vrm/VRMA_MotionPack/*.glb") +
               glob.glob("/home/rab/code/vrm/animation/*.vrma") +
               glob.glob("/home/rab/code/vrm/animation/*.glb"))
idx = 0

# Spring params (1.0 = use VRM file defaults, higher = more effect)
stiff, drag, grav = 1.0, 1.0, 1.0

# Blend params
blend_dur = 2.0
loop_blend = False
smooth_loop = False

# Axis correction mode
axis_mode = 3  # Default rotY 180
axis_names = ["none", "auto", "rotX 180", "rotY 180", "rotZ 180"]

def play(i, blend=False):
    cmd({"action": "load_animation", "path": anims[i]})
    if blend:
        cmd({"action": "blend_to", "index": 0, "duration": blend_dur})
    cmd({"action": "play"})
    print(f"[{i+1}/{len(anims)}] {os.path.basename(anims[i])}" + (" (blended)" if blend else ""))

def show_spring():
    cmd({"action": "set_spring", "stiffness": stiff, "drag": drag, "gravity": grav})
    print(f"  Spring: stiff={stiff:.1f} drag={drag:.1f} grav={grav:.1f}")

def show_blend():
    print(f"  Blend: duration={blend_dur:.2f}s loop={loop_blend} smooth={smooth_loop}")

def set_axis(mode):
    global axis_mode
    axis_mode = mode
    cmd({"action": "set_anim_correction", "mode": mode})
    print(f"  Axis correction: {axis_names[mode]}")

old = termios.tcgetattr(sys.stdin)
try:
    tty.setcbreak(sys.stdin.fileno())
    print("n/p=anim, b=blend, l=loop, [/]=dur, 0-4=axis, 7-0=spring, r=reset, q=quit")
    show_spring()
    show_blend()
    play(idx)
    while True:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            k = sys.stdin.read(1)
            if k in "qQ": break
            elif k in "nN ": idx = (idx + 1) % len(anims); play(idx)
            elif k in "pP": idx = (idx - 1) % len(anims); play(idx)
            elif k in "bB": idx = (idx + 1) % len(anims); play(idx, blend=True)
            elif k in "lL":
                loop_blend = not loop_blend
                cmd({"action": "set_loop_blend", "duration": blend_dur if loop_blend else 0})
                show_blend()
            elif k in "sS":
                smooth_loop = not smooth_loop
                cmd({"action": "set_smooth_loop", "enabled": smooth_loop})
                show_blend()
            elif k == '[': blend_dur = max(0.05, blend_dur - 0.1); show_blend()
            elif k == ']': blend_dur = min(10.0, blend_dur + 0.1); show_blend()
            # Axis correction: a + number
            elif k == 'a':
                # Read next char for axis mode
                if select.select([sys.stdin], [], [], 0.5)[0]:
                    k2 = sys.stdin.read(1)
                    if k2 in '01234':
                        set_axis(int(k2))
            # Spring controls: s + 1/2/3 for stiff/drag/grav, then +/-
            elif k == '7': stiff = min(3.0, stiff + 0.1); show_spring()
            elif k == '8': stiff = max(0.0, stiff - 0.1); show_spring()
            elif k == '9': drag = min(3.0, drag + 0.1); show_spring()
            elif k == '0': drag = max(0.0, drag - 0.1); show_spring()
            elif k == '-': grav = max(0.0, grav - 0.2); show_spring()
            elif k == '=': grav = min(5.0, grav + 0.2); show_spring()
            elif k == 'r': cmd({"action": "reset_spring"}); print("  Springs reset")
    print(f"\n=== Final values ===")
    print(f"spring: stiffness={stiff:.2f} drag={drag:.2f} gravity={grav:.2f}")
    print(f"blend: duration={blend_dur:.2f}s loop={loop_blend} smooth={smooth_loop}")
    print(f"axis: {axis_names[axis_mode]}")
finally:
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
    sock.close()
