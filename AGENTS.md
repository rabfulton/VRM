# Project guide (vrm-overlay)

This repository builds a Linux/X11 OpenGL avatar renderer with JSON IPC and a
separate Python/PySide6 Qt Quick controller. Read [README.md](README.md) for setup.

## Layout and ownership

- `src/`: renderer, loader, animation, spring simulation, contacts, IPC and audio.
- `vrm_overlay/`: reusable Python client, speech providers, Qt controller and QML.
- `control.py`: Studio launcher; the renderer has no embedded GUI.
- `deps/`: vendored libraries and the ozz-animation submodule.
- `tests/`: maintained native/Python/QML regressions; `scripts/`: speech setup.
- `examples/chat_tts/`: independent typed-chat and sentence-speech prototype.
- `docs/`: current guides; `animation/README.md`: local asset setup.
- `build/`, `deps/_build/`: generated output, tests and optional speech downloads.

Avatars, animations, model weights and local settings are ignored. Preserve users'
local copies and third-party notices. Do not add generated binaries or objects.

## Runtime modules

- `src/main.c`: integration, keyboard controls, IPC request handlers and state.
- `src/ipc.c/.h`: NDJSON server; default `/tmp/vrm-overlay.sock`, one controller.
- `src/gltf_loader.c/.h`, `gltf_loader_impl.cpp`: VRM/glTF loading and expressions.
- `src/renderer.c/.h`: live embedded shaders, skinning, GPU morphs and draw path.
- `src/animator.c/.h`, `ozz_bridge.cpp/.h`, `transition.c/.h`: animation sampling
  and inertial transition corrections; record pose history before contact IK.
- `src/foot_lock.cpp/.h`: flat-floor IK, raw-target classification and smooth release.
- `src/pose.c`: shared world-transform propagation used by runtime and IK tests.
- `src/spring_bones.c/.h`: fixed 120 Hz springs and garment/arm proxy contacts.
- `src/audio.c/.h`, `viseme.c/.h`: miniaudio playback, PCM ring and default lip sync.
- `src/event.c/.h`: continuous `EventStream` data and discrete multi-clock scheduler.
- `vrm_overlay/qt_controller.py`, `qml/`: asynchronous IPC and Studio lifecycle.
- `vrm_overlay/tts.py`, `tts_install.py`, `tts_models.py`, `qt_speech.py`: selectable
  local speech, verified installation and asynchronous playback. Nano/CrispASR on
  Vulkan is the default; CPU is explicitly selectable.

## Timing and morphs

Use audio sample indices for playback/lip sync, and monotonic nanoseconds for
wall-time actions. The scheduler updates with `event_update_seconds(now_ns)` and
`event_update_audio(now_samples)`. Despite its name, `EVENT_DOMAIN_SECONDS` stores
integer nanoseconds. Springs use a bounded fixed-step accumulator.

VRM expression metadata maps to morph binds. `gltf_set_expression()` updates
weights; packed GPU morph deltas are sampled by the vertex shader. If expressions
do not move, check packing limits, `GL_MAX_TEXTURE_SIZE` and uniform wiring.

Do not call `audio_read()` from another analysis consumer: it advances playback.
Analyze at ingest or use a non-destructive tap/FIFO.

## Documentation

- [API](docs/api.md): current IPC reference.
- [Studio](docs/studio.md), [speech](docs/tts.md): user controls and provider setup.
- [Architecture](docs/architecture.md): ownership, pipeline and event clocks.
- [Motion](docs/motion.md), [foot planting](docs/foot-planting.md): algorithms/limits.
- [Development](docs/development.md): build variants, assets and tests.
- [Roadmap](docs/roadmap.md): outstanding product work.

## Build and verification

Build with `make`; run `python3 control.py --model /path/to/avatar.vrm`, or
`./vrm-overlay /path/to/avatar.vrm`. Initialize the ozz submodule before building.
`USE_OZZ=0` omits ozz sampling/transitions and leg IK; run `make clean` when changing
this feature flag. There is no Jolt dependency.

Use `make test` for headless native/Python regressions and `make test-sanitize`
for ASan/UBSan. PySide6 enables optional offscreen QML interaction coverage.
`make test-ui` needs X11/audio and local assets; `make test-speech` also needs a
prepared real voice or download access. Paths and reference-fixture checks are
documented in the development guide. Keep synthetic tests asset-independent.
