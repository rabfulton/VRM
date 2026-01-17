# VRM Overlay IPC API

The VRM overlay accepts commands via NDJSON over a UNIX domain socket at `/tmp/vrm-overlay.sock`.

## Connection

```python
import socket, json

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/tmp/vrm-overlay.sock")

def cmd(c):
    sock.send((json.dumps(c) + "\n").encode())
    return json.loads(sock.recv(4096))
```

## Commands

### Model Loading

#### load
Load a VRM model file.
```json
{"action": "load", "path": "/path/to/model.vrm"}
```

#### load_animation
Load an animation file (VRMA or GLB).
```json
{"action": "load_animation", "path": "/path/to/animation.vrma"}
```

### Playback Control

#### play
Start animation playback.
```json
{"action": "play"}
```

#### pause
Pause animation playback.
```json
{"action": "pause"}
```

#### set_animation
Switch to animation by index.
```json
{"action": "set_animation", "index": 0}
```

#### blend_to
Crossfade to animation over duration (seconds), with optional loop/count.
```json
{"action": "blend_to", "index": 0, "duration": 0.5, "loop": true}
```
```json
{"action": "blend_to", "index": 0, "duration": 0.5, "count": 3}
```
- `loop`: If true, loop indefinitely; if false, play once
- `count`: Number of times to play the clip (1 = play once). Overrides auto-loop detection.

### Model Transform

#### set_position
Set model position offset in world units.
```json
{"action": "set_position", "x": 0.0, "y": 0.0, "z": 0.0}
```
All parameters are optional - only specified values are updated.

#### set_rotation
Set model rotation in degrees (pitch/yaw/roll).
```json
{"action": "set_rotation", "pitch": 0.0, "yaw": 0.0, "roll": 0.0}
```
- `pitch` - rotation around X axis (tilt forward/back)
- `yaw` - rotation around Y axis (turn left/right)  
- `roll` - rotation around Z axis (tilt sideways)

All parameters are optional - only specified values are updated.

### Spring Bones

#### set_spring
Adjust spring bone physics multipliers (1.0 = default from VRM file).
```json
{"action": "set_spring", "stiffness": 1.0, "drag": 1.0, "gravity": 1.0}
```

### Animation Looping

#### set_seam_blend
Set the loop seam crossfade duration (seconds) used when forced-looping a non-loopable clip (0 disables).
```json
{"action": "set_seam_blend", "duration": 0.15}
```
If `duration` is omitted, the current value is returned.
Response:
```json
{"id": 0, "status": "ok", "data": {"seam_blend_time": 0.15}}
```

#### reset_spring
Reset spring bones to rest pose.
```json
{"action": "reset_spring"}
```

### Animation Correction

#### set_anim_correction
Set axis correction for animations with different coordinate systems.
```json
{"action": "set_anim_correction", "mode": 3}
```
Modes:
- 0: none
- 1: auto-detect
- 2: rotate X 180°
- 3: rotate Y 180° (default)
- 4: rotate Z 180°

### Expressions (Morphs)

#### set_expression
Set a named expression weight (0.0 to 1.0).
```json
{"action": "set_expression", "name": "A", "weight": 1.0}
```
Supports standard VRM presets (`A`, `I`, `U`, `E`, `O`, `Blink`, `Joy`, etc.) and mapped aliases (`aa`, `ih`, `blink_l`, etc.).

#### list_expressions
Get a list of all available expression names on the current model.
```json
{"action": "list_expressions"}
```
Response:
```json
{"id": 0, "status": "ok", "data": {"expressions": ["A", "I", "U", "E", "O", "Blink", ...]}}
```

### Audio Control

#### play_audio_file
Load and play a WAV file from disk.
```json
{"action": "play_audio_file", "path": "/path/to/voice.wav"}
```
This loads the entire file into the internal ring buffer.

#### audio_stream_begin
Reset the audio buffer and prepare for a new stream.
```json
{"action": "audio_stream_begin"}
```

#### audio_stream_chunk
Stream a chunk of 24kHz 16-bit mono PCM audio (base64 encoded).
```json
{"action": "audio_stream_chunk", "data": "BASE64_ENCODED_PCM_BYTES..."}
```

#### audio_stream_end
Mark the end of an audio stream.
```json
{"action": "audio_stream_end"}
```

### Lip Sync

Lip sync is automatically enabled when `audio_stream_begin` is called. The avatar's mouth expressions will animate based on audio analysis.

#### lip_sync_enable
Manually enable or disable lip sync.
```json
{"action": "lip_sync_enable", "enabled": true}
```

#### lip_sync_config
Tune lip sync analysis parameters.
```json
{"action": "lip_sync_config", "gain": 1.0, "threshold": 0.02, "smoothing": 0.3}
```
- `gain` - Input gain multiplier (default 1.0). Increase for quieter audio.
- `threshold` - RMS below this is treated as silence (default 0.02). Increase to ignore background noise.
- `smoothing` - Interpolation factor 0-1 (default 0.3). Higher = smoother but more latency.

All parameters are optional - only specified values are updated.

### Window Control

#### set_visible
Show or hide the overlay window.
```json
{"action": "set_visible", "visible": true}
```

### Application Control

#### shutdown
Request the overlay to exit cleanly.
```json
{"action": "shutdown"}
```

### Auto-Blink

#### auto_blink_enable
Enable or disable automatic blinking. Returns `event_id` when enabling.
```json
{"action": "auto_blink_enable", "enabled": true}
```
Response when enabling:
```json
{"id": 0, "status": "ok", "event_id": 42}
```

#### auto_blink_config
Configure auto-blink parameters.
```json
{"action": "auto_blink_config", "rate": 14, "duration": 150}
```
- `rate`: Blinks per minute (1-60, default 14)
- `duration`: Blink duration in ms (50-500, default 150)

## Response Format

All commands return a JSON response:
```json
{"id": 0, "status": "ok"}
```

On error:
```json
{"id": 0, "status": "error", "error": {"message": "description"}}
```
