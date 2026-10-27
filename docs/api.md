# VRM Overlay IPC API

The VRM overlay accepts commands via NDJSON over a UNIX domain socket at `/tmp/vrm-overlay.sock`.
Override it with `./vrm-overlay --socket PATH [MODEL.vrm]`.

## Connection

```python
from vrm_overlay import IpcClient

client = IpcClient()
cmd = client.request
print(cmd({"action": "ping"}))
client.close()
```

The server services one active connection. Each request must be a JSON object with
a nonempty string `action` and optional signed 32-bit integer `id` (defaults to 0).
Replies echo `id` and contain `status: "ok"` or `status: "error"`; errors contain
`error.message`. The shared Python client supplies IDs and raises `IpcError` on errors.

Requests are limited to 65,536 bytes including the newline. An oversized unfinished
line disconnects its client; split PCM into small chunks (2,048 PCM bytes is a useful
default). Responses use queued nonblocking output; clients exceeding 1 MiB of pending
output are disconnected. Invalid JSON/actions receive errors and leave the connection
usable. A timeout/disconnect does not establish whether a command ran: do not replay
commands with side effects without checking state.

`audio_stream_chunk` accepts an entire chunk or rejects it without accepting any
samples. Its data must be valid base64 containing complete 16-bit PCM samples.
`audio_stream_begin` clears previous audio and restarts the stream-relative sample
clock. `audio_stream_end` marks input complete and lets queued audio finish; its
acknowledgement does not mean playback has finished. Poll `audio_status.data.drained`
to detect an empty, closed stream. This describes the playback queue; the device may
still have output latency. Status also reports `stream_open` and `active` (device started).

`{"action": "audio_cancel"}` clears queued playback and lip sync, closes the stream
and resets its sample clock. Audio already submitted to an output device is subject
to that backend's stop/latency behavior.

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
- 3: rotate Y 180°
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
Playback is limited by the available ring-buffer capacity; this is intended for
short WAV clips. Stream PCM through `audio_stream_*` with `audio_status`
backpressure for longer audio.

#### audio_status
Query the current audio ring buffer status.
```json
{"action": "audio_status"}
```
Response:
```json
{
  "id": 0,
  "status": "ok",
  "data": {
    "sample_rate": 24000,
    "channels": 1,
    "active": true,
    "available_samples": 12345,
    "free_samples": 67890,
    "total_written": 123456,
    "total_read": 111111,
    "available_seconds": 0.51,
    "free_seconds": 2.83
  }
}
```

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

Lip sync starts enabled. WAV and streamed PCM playback drive mouth expressions
automatically; `audio_stream_begin` also re-enables analysis.

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

## Controller state and tuning

`get_state` returns a `data` object with `api_version: 1` and the runtime's current
model/animation paths, loaded clips, animation index/time/duration, playback state,
speed/loop, backend/availability, expressions, blinking, lip-sync, spring parameters,
blend settings, camera, overlay visibility/scale/shader and frame rate. It does not
change runtime state. Qt Studio polls this snapshot to keep controls synchronized.

```json
{"action":"get_state"}
{"action":"set_playback","speed":0.5,"loop":true}
{"action":"set_blend","duration":0.4,"seam":0.15}
{"action":"set_blend","group":3,"factor":0.9}
{"action":"set_spring","stiffness":1.2,"cloth_mode":true,"cloth_drag":1.1}
{"action":"save_tuning"}
{"action":"set_render","shader":"anime","always_on_top":true,"scale":1.0}
{"action":"clear_expressions"}
```

- `set_playback`: speed 0.1–3 and/or boolean loop; preserves play/pause state.
- `set_blend`: duration 0–10 seconds, seam 0–1 second, or integer group index and
  duration multiplier 0–2. Group indices: root, spine, head, arms, hands, legs, feet, face, other.
- `set_spring`: stiffness/gravity 0–5, drag 0–3; boolean `cloth_mode`;
  `cloth_stiffness`, `cloth_drag`, `cloth_gravity` 0–3; `cloth_taper` 0–1.
  Invalid values reject the entire update. Cloth profile changes reset springs.
- `save_tuning`: saves spring and seam settings alongside the loaded animation;
  errors if there is no current animation or the file cannot be written. Global
  transition duration and group blend factors are not persisted by this command.
- `set_render`: shader `anime`, `toon` or `pbr`; boolean `always_on_top`; scale 0.1–10.
  Scale changes also update the camera framing. Invalid options reject the update.
- `clear_expressions`: sets every available expression to zero. Automatic blinking
  and lip sync can continue to drive their expressions while enabled.

### Motion diagnostics and transition timing

`get_state.data.physics` reports `garment_collisions`, `garment_chains`,
`arm_colliders`, `generated_arm_colliders`, `contacts`, `unresolved_contacts`,
`max_penetration`, `garment_arm_penetration`, `steps` and `dropped_time`.
Penetrations are in model units (metres for VRM); counts/residuals describe the
last physics update. Garment/arm overlap is measured even when that policy is off.
These are collision-proxy diagnostics, not measurements of skinned mesh triangles.

```json
{"action":"set_spring","garment_collisions":true}
```

The new boolean is validated atomically with other spring options and resets
spring history when changed. It augments only conventionally named garment chains
with authored arm volumes or conservative humanoid capsule fallbacks. It is session
state, not an animation-file tuning setting.

`get_state.data.blend` additionally reports `active` and `progress`. Ozz transitions
start at clip time zero and preserve the outgoing animation-layer pose/velocity
before foot IK. The persistent foot-contact layer is applied once after blending,
including across reloads and interruptions. Duration counts
unpaused real update time, independently of playback speed. Seeking cancels a
transition. Group factors now multiply duration: 0 = immediate, 0.5 = half as long,
1 = normal, 2 = twice as long. Factors are captured at transition start, and all
joints reach the complete target pose. Default master duration is 0.45 seconds.
The legacy backend still switches immediately. See [motion behaviour](motion.md) for implementation
and contact/foot-locking limitations.

### Foot planting and lip-sync defaults

Foot planting is enabled by default when supported humanoid leg chains and the
compiled ozz IK solver are available. It is independent of the sampling backend.

```json
{"action":"set_foot_lock","enabled":true}
```

The command requires a boolean and clears current contact history. State is
session-local. `get_state.data.foot_lock` contains `enabled`, supported-leg count
`supported`, `pelvis_offset`, `floor_offset`, and `left`/`right` objects. Each object
reports `supported`, `planted`, `weight`, `error`, target `speed` and `height`,
`ground_height`, `anchor`, and (for supported legs) current `position`. Distances
are in model units (metres for VRM). Target height is relative to the calibrated
clip floor; the anchor plane stays at the bind-pose ankle height. See [foot planting](foot-planting.md).

Lip sync now starts enabled and is automatic in Studio. `play_audio_file` and PCM
ingest require no preceding enable command. The GUI has no lip-sync toggle.
`lip_sync_enable` is retained as an advanced compatibility/debug command; explicit
callers can still disable the analyzer. A new stream enables it as before.

### Chat open request

`get_state.data.chat_request_seq` is an optional unsigned 32-bit sequence counter.
A right-button press on the avatar increments it; Studio notices a changed value
and opens chat near the pointer. Controllers should establish a baseline on
connect and not replay a previous session's request. Older runtimes omit the
field. Chat API credentials and conversation handling live in Studio, not in
the renderer IPC server.

### Animation behaviour metadata

Sibling `.cfg` files may set `loopable=0|1`, `startup=0|1` and `is_idle=0|1`.
When `loopable=0`, requests to repeat through `set_playback.loop=true`,
`blend_to.loop=true` or `blend_to.count>1` are rejected. The renderer keeps its
geometric loop heuristic for automatic choices when the flag is absent.

`get_state.data.animation_properties` contains the booleans `loopable`,
`explicit_loopable`, `startup` and `is_idle`. `loopable` reports repeat permission;
`loop` in the surrounding state still reports the current playback choice.

```json
{"action":"reload_animation_metadata"}
```

Reloads the current animation's flags from disk without seeking, resetting springs
or reloading the asset. Returns an error if no animation is loaded or its config
cannot be read. Disallowing repeats stops further loops after the current pass.
Studio coordinates startup/idle behaviours; see [animation properties](animation-properties.md).

### Rendered frame bounds

```json
{"action":"get_frame_bounds"}
```

Optional, synchronous readback diagnostic for clipping checks. Reports framebuffer
`width`, `height`, `visible_pixels`, `edge_pixels`, and alpha-bound `min_x`, `max_x`,
`min_y`, `max_y` in top-left pixel coordinates. Pixels with alpha greater than 2/255
count as visible. Empty frames use -1 bounds. MSAA is resolved offscreen for stable
readback under desktop occlusion. Avoid polling this every frame; it allocates a
readback buffer and synchronizes with the GPU. See [fitting tests](window-fitting.md).
