# VRM Overlay IPC Protocol

Control the VRM overlay viewer via UNIX socket using JSON messages.

## Connection

```python
import socket
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/vrm-overlay.sock')
```

## Protocol

- **Transport**: UNIX domain socket at `/tmp/vrm-overlay.sock`
- **Format**: NDJSON (newline-delimited JSON)
- **Request**: `{"id": <int>, "action": "<string>", ...params}`
- **Response**: `{"id": <int>, "status": "ok"|"error", "error": {"message": "..."}}`

## Commands

### ping
Health check.
```json
{"id": 1, "action": "ping"}
```

### load
Load a VRM/glTF model.
```json
{"id": 1, "action": "load", "path": "/path/to/model.vrm"}
```

### unload
Unload the current model.
```json
{"id": 1, "action": "unload"}
```

### load_animation
Load animation from a VRMA file onto the current model.
```json
{"id": 1, "action": "load_animation", "path": "/path/to/animation.vrma"}
```

### play
Start animation playback.
```json
{"id": 1, "action": "play", "speed": 1.0}
```
- `speed`: Playback speed multiplier (default: 1.0)

### pause
Pause animation playback.
```json
{"id": 1, "action": "pause"}
```

### set_time
Seek to a specific time in the animation.
```json
{"id": 1, "action": "set_time", "t": 2.5}
```
- `t`: Time in seconds

### set_animation
Select an animation clip by index.
```json
{"id": 1, "action": "set_animation", "index": 0}
```

### blend_to
Blend to an animation clip with optional loop/count.
```json
{"id": 1, "action": "blend_to", "index": 1, "duration": 0.35, "loop": true}
```
```json
{"id": 1, "action": "blend_to", "index": 2, "duration": 0.35, "count": 3}
```
- `duration`: Blend time in seconds (default: 0.3)
- `loop`: If true, loop indefinitely; if false, play once
- `count`: Number of times to play the clip (1 = play once). Overrides auto-loop detection.

### set_camera
Control the orbit camera.
```json
{"id": 1, "action": "set_camera", "azimuth": 0, "polar": 1.57, "dist": 3, "target": [0, 0.8, 0]}
```
- `azimuth`: Horizontal angle in radians
- `polar`: Vertical angle in radians (0=top, π=bottom)
- `dist`: Distance from target
- `target`: [x, y, z] look-at point

### set_window
Position and resize the overlay window.
```json
{"id": 1, "action": "set_window", "x": 100, "y": 100, "width": 400, "height": 600}
```

### set_visible
Show or hide the window.
```json
{"id": 1, "action": "set_visible", "visible": true}
```

### quit
Close the application.
```json
{"id": 1, "action": "quit"}
```

## Example

```python
import json, socket

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/vrm-overlay.sock')
f = sock.makefile()

def call(action, **params):
    req = {"id": 1, "action": action, **params}
    sock.sendall((json.dumps(req) + "\n").encode())
    return json.loads(f.readline())

# Load model and animation
call("load", path="/path/to/model.vrm")
call("load_animation", path="/path/to/animation.vrma")
call("play", speed=1.0)
```
