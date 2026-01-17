# vrm-overlay

`vrm-overlay` is a small OpenGL renderer + IPC-controlled runtime for loading VRM/glTF avatars, playing VRMA/GLB animations, streaming/playing audio, and driving expressions/morph targets (including lip sync). It’s designed as a lightweight “overlay-style” avatar runtime that you control from another process.

## Features

- Load VRM models (VRM 0.x prioritized; VRM 1.0 best-effort).
- Play VRMA/GLB animations with crossfades and loop control.
- Spring bone simulation (including a “cloth” spring mode toggle).
- Expression/morph control via a named expression map (VRM presets + aliases).
- Audio playback + streaming (24kHz, 16-bit mono PCM) with lip-sync analysis.
- JSON IPC server over a UNIX domain socket (`/tmp/vrm-overlay.sock`, NDJSON).
- Built-in debug UI window and a small interactive controller script (`test.py`).

## Quick Start

Build:

```sh
make
```

Run the overlay:

```sh
./vrm-overlay
```

Then in another terminal, use the interactive test harness (spawns the overlay automatically if you run it directly):

```sh
./test.py
```

## Platform

Currently targets Linux/X11 via GLFW + OpenGL (see link flags in `Makefile`).

## IPC API

The overlay listens on `/tmp/vrm-overlay.sock` and accepts newline-delimited JSON (NDJSON).

- Full reference: `API.md`
- Design/protocol notes: `VRM.md` and `EVENT_REDESIGN.md`

Minimal Python example (send one command, wait for one response):

```python
import socket, json

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/tmp/vrm-overlay.sock")

sock.send((json.dumps({"id": 1, "action": "load", "path": "test1.vrm"}) + "\n").encode())
print(sock.recv(4096).decode())
```

Typical command sequence:

```json
{"action":"load","path":"test1.vrm"}
{"action":"set_visible","visible":true}
{"action":"load_animation","path":"animation/<clip>.vrma"}
{"action":"blend_to","index":0,"duration":0.5,"loop":true}
{"action":"play"}
```

Common commands:

```json
{"action":"set_expression","name":"A","weight":1.0}
{"action":"play_audio_file","path":"test.wav"}
{"action":"auto_blink_enable","enabled":true}
```

## Controls

### Overlay window (built-in)

- `q` / `Esc`: quit
- `t`: toggle always-on-top
- `c`: toggle shader mode (e.g. cel shading)
- `b`: toggle spring “cloth mode”
- `1`–`5`: animation axis correction presets

### `test.py` (interactive IPC driver)

`test.py` provides hotkeys for animation switching/blending, spring tuning, expressions, and audio + lip sync. See the header comment in `test.py` for the complete key list.

## Build Notes

### Dependencies

Builds are driven by `make` and use `pkg-config` for system libraries:

- C/C++ toolchain (`gcc`/`g++`)
- `glfw3`, `epoxy`
- `pkg-config`
- `cmake` (only needed for the optional ozz/Jolt integration; enabled by default)

### Optional ozz/Jolt integration

By default, `make` builds and links a local copy of:

- `deps/ozz-animation` (animation runtime)
- `deps/JoltPhysics` (physics)

To build without them:

```sh
make USE_OZZ_JOLT=0
```

## Project Layout

- `src/`: runtime (renderer, loader, animation, IPC, audio, events, UI)
- `deps/`: vendored third-party libraries
- `animation/`: sample `.vrma`/`.glb` animations
- `test.py`: interactive controller (IPC client)
- `API.md`: up-to-date IPC/API reference and examples
- `VRM.md`, `VRMA.md`, `SPEECH.md`, `EVENT_REDESIGN.md`, `UI.md`: design notes

## Troubleshooting

- **IPC socket issues**: if the process crashes, you may need to remove a stale socket: `rm -f /tmp/vrm-overlay.sock`.
- **Build fails on missing libs**: ensure `pkg-config --cflags glfw3 epoxy` works (install your distro’s `glfw` + `libepoxy` development packages).
- **Lip sync timing**: for best alignment, stream audio and drive mouth shapes from audio sample time (see notes in `SPEECH.md` and the event system design in `EVENT_REDESIGN.md`).
