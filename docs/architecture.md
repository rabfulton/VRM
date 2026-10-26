# Architecture and timing

The renderer and Studio are separate processes. `control.py` launches the
PySide6/Qt Quick workspace; `vrm-overlay` owns the transparent GLFW/OpenGL window,
audio output and model state. Linux/X11 is the supported renderer platform.

## Runtime modules

| Area | Sources | Responsibility |
| --- | --- | --- |
| Integration | `src/main.c` | Main loop, IPC handlers and authoritative state snapshot |
| IPC | `src/ipc.c`, `vrm_overlay/client.py` | NDJSON framing, request IDs, queued responses and socket ownership |
| Models | `src/gltf_loader.c`, `src/gltf_loader_impl.cpp` | VRM/glTF loading, humanoid mapping, morphs and expression metadata |
| Rendering | `src/renderer.c` | Embedded shaders, skinning, GPU morphs and draw passes |
| Animation | `src/animator.c`, `src/ozz_bridge.cpp`, `src/transition.c` | Sampling, retargeting and inertial transition corrections |
| Contacts | `src/foot_lock.cpp`, `src/pose.c` | Leg IK, contact history and shared world-transform propagation |
| Secondary motion | `src/spring_bones.c` | Fixed-step hair/clothing simulation and proxy contacts |
| Audio | `src/audio.c`, `src/viseme.c` | Miniaudio playback, PCM ring buffer and mouth analysis |
| Scheduling | `src/event.c` | Continuous keyframe streams and discrete timed events |
| Studio | `vrm_overlay/qt_controller.py`, `vrm_overlay/qml/` | Asynchronous IPC, renderer lifecycle, confirmed state and controls |
| Speech | `vrm_overlay/tts.py`, `qt_speech.py`, `tts_install.py`, `tts_models.py` | Provider configuration, cancellable synthesis and pinned downloads |

Ozz supplies animation sampling and two-bone IK. Spring simulation is implemented
in this repository; it has no Jolt dependency. The optional `USE_OZZ=0` build
retains legacy sampling but omits ozz transitions and leg IK.

## Pose pipeline

Animation supplies the raw target pose used to classify foot contacts. Transition
corrections produce the displayed animation pose; its pose/velocity history is
recorded before IK. Foot planting then applies leg rotations and bounded pelvis
correction. Spring simulation supplies secondary motion before rendering.

Keeping transition history before IK prevents contact corrections from being
applied twice during a transition restart or animation reload. Springs advance
at 120 Hz through a bounded accumulator, independently of render frame rate.
See [motion](motion.md) and [foot planting](foot-planting.md) for the exact policies.

VRM 0.x and 1.0 expression metadata are parsed into an expression map.
`gltf_set_expression()` applies its morph binds. Vertex deltas are packed into
a texture and consumed by the live shaders embedded in `src/renderer.c`.

## Clocks and events

Audio streams use 24 kHz mono signed 16-bit PCM. Playback and lip sync should use
**audio sample time**, rather than packet arrival time or renderer frames.
General timed actions use **monotonic wall time**, stored as integer nanoseconds.
Animation playback has its own speed/pause-controlled time; transition decay uses
unpaused elapsed update time.

`src/event.c` provides interpolated `EventStream` keyframes and a discrete
scheduler with a fixed event pool, per-domain heaps, lifecycle lists and an SPSC
command queue. `EVENT_DOMAIN_SECONDS` is nanosecond wall time despite its name;
`EVENT_DOMAIN_AUDIO_SAMPLES` is an integer sample index. Callers update these with
`event_update_seconds(now_ns)` and `event_update_audio(now_samples)`.

Do not use `audio_read()` from another analysis consumer: it advances playback's
read position. Add analysis at ingest or through a non-destructive tap/FIFO.

## Ownership and cancellation

Studio uses one asynchronous IPC connection for controls and speech playback.
Its ordered transactions stop on failure, slider edits are coalesced, and polls
update confirmed state without writing it back. Timed-out commands have unknown
outcomes and are not automatically replayed.

Speech inference runs outside the GUI event loop. CrispASR is an authenticated
loopback sidecar with resident models. Cancel invalidates pending results, clears
queued playback and terminates owned inference; a later utterance can restart it.
Provider changes do not change renderer PCM transport or lip-sync timing.
