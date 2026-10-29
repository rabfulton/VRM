# Project guide (domovushka)

This repository builds a Linux/X11 and native Wayland OpenGL avatar renderer with
JSON IPC and a separate Python/PySide6 Qt Quick controller. Read [README.md](README.md)
for setup.

## Layout and ownership

- `src/`: renderer, loader, animation, spring simulation, contacts, IPC and audio.
- `vrm_overlay/`: reusable Python client, speech providers, Qt controller and QML.
- `domovushka`: application launcher; `control.py` remains a compatibility entry
  point. The renderer has no embedded GUI.
- `deps/`: vendored libraries and the ozz-animation submodule.
- `tests/`: maintained native/Python/QML regressions; `scripts/`: speech setup.
- `examples/chat_tts/`: independent typed-chat and sentence-speech prototype.
- `docs/`: current guides; `animation/README.md`: local asset setup.
- `assets/starter/`: audited CC0 starter avatars, motions, notices and hashes;
  `scripts/build_starter_assets.py`: pinned, dependency-free source conversion.
- `build/`, `deps/_build/`: generated output, tests and optional speech downloads.

Additional user avatars/animations, model weights and local settings are ignored.
The versioned starter pack is the explicit asset exception. Preserve users'
local copies and third-party notices. Do not add generated binaries or objects.

## Runtime modules

- `src/main.c`: integration, keyboard controls, IPC request handlers and state.
- `src/platform.c`, `platform_x11.c`, `platform_wayland.c`: backend selection,
  surface/window lifecycle and presentation. Maintained targets are Xorg and
  Hyprland; monitor selection is deferred. See `docs/wayland.md`.
- `src/ipc.c/.h`: NDJSON server; one controller. `app_paths.c` and Python `paths.py`
  share private XDG runtime socket defaults and renderer settings paths.
- `src/gltf_loader.c/.h`, `gltf_loader_impl.cpp`: VRM/glTF loading and expressions.
- `src/bounds.c`: skinned geometry envelopes, in-memory full-clip bounds and
  pixel-stable transparent viewport expansion. Legacy 2D bounds caches are ignored.
- `src/renderer.c/.h`: live embedded shaders, skinning, GPU morphs and draw path.
- `src/input_mask.c/.h`, `hit_capture.c/.h`: avatar-only stencil coverage,
  nonblocking fenced GPU transfers and X11 bitmap input shape. Keep shadow-only
  pixels/transparent gaps click-through; share the mask with mouse hit testing.
- `src/expressions.c`: logical expression weights, additive bounded composition
  and smooth speech-aware emotion attenuation; preserve vowel and blink inputs.
- `src/anim_config.c/.h`, `vrm_overlay/animation_config.py`: shared per-animation
  behaviour flags; preserve unrelated tuning/cache data when saving. Studio owns
  startup/idle selection; native animation code enforces explicit loop restrictions.
  Pose metadata routes automatic actions through one-shot bridges; per-clip
  floor-contact policy must preserve the global user foot-lock preference.
  `avatar_catalog.py` selects each starter avatar's matching animation library.
- `src/animator.c/.h`, `ozz_bridge.cpp/.h`, `transition.c/.h`: animation sampling
  and inertial transition corrections; record pose history before contact IK.
- `src/walk_motion.c/.h`, `vrm_overlay/qt_walking.py`: horizontal humanoid walking.
  Native code owns travel timing, yaw and a fixed local viewport; the controller
  owns bounded session jobs, cancellation and return to idle. Duration estimates
  are advisory; completion requires actual arrival and a finished idle blend.
- `src/foot_lock.cpp/.h`: flat-floor IK, raw-target classification and smooth release.
- `src/pose.c`: shared world-transform propagation used by runtime and IK tests.
- `src/spring_bones.c/.h`: fixed 120 Hz springs and garment/arm proxy contacts.
- `src/audio.c/.h`, `viseme.c/.h`: miniaudio playback, PCM ring and default lip sync.
- `src/event.c/.h`: continuous `EventStream` data and discrete multi-clock scheduler.
- `vrm_overlay/qt_controller.py`, `qml/`: asynchronous IPC and Studio lifecycle.
  `qt_gem_menu.py` owns the extensible action registry and per-avatar menu theme;
  `menu_design.py` defines palette sampling, layout and shared vector icons.
  Qt and `src/gem_menu*.c` draw the same bounded spec; preserve native Wayland
  popup/grab ownership and shared-buffer release rules. See `docs/gem-menu.md`.
  `visibility.py` owns hide/exit/summon/entrance across UI and MCP. Shortcut helpers
  in `qt_shortcut.py` / `visibility_shortcut.py` own Xorg grabs or temporary
  Hyprland bindings; never overwrite existing compositor bindings/configuration.
  Studio starts hidden; closing a window keeps the background controller alive.
  Overlay right-click requests open the independent QML menu; explicit Quit stops
  the companion. Automatically owned idles rotate; manual playback/pause cancels
  ownership until Resume idle. Preserve that distinction for behaviour tests.
  Startup loads use deferred presentation until play, with no bind-pose blend.
- `vrm_overlay/qt_chat.py`, `qt_chat_network.py`, `chat.py`, `credentials.py`: streamed
  API conversation, OS-only keyring storage and a validated cue catalogue.
  `animation_library.py` assigns safe cue IDs; library checkboxes persist LLM access
  per folder. Access changes must invalidate pending chat actions.
  Keep secrets out of settings/logs/prompts; advertise only implemented capabilities.
- `vrm_overlay/tts.py`, `tts_install.py`, `tts_models.py`, `qt_speech.py`: selectable
  local speech, verified installation and asynchronous playback. Nano/CrispASR on
  Vulkan is the default; CPU is explicitly selectable.

## Timing and morphs

Use audio sample indices for playback/lip sync, and monotonic nanoseconds for
wall-time actions. The scheduler updates with `event_update_seconds(now_ns)` and
`event_update_audio(now_samples)`. Despite its name, `EVENT_DOMAIN_SECONDS` stores
integer nanoseconds. Springs use a bounded fixed-step accumulator.
`frame_clock.h` caps presentation at 60 FPS and averages completed frames; keep
that clock independent of animation/load timestamps and audio sample clocks.
Owned one-shots schedule a completion refresh instead of waiting for UI polling.

VRM expression metadata maps to morph binds. `gltf_set_expression()` records
logical inputs and composes weights; packed GPU morph deltas are sampled by the vertex shader. If expressions
do not move, check packing limits, `GL_MAX_TEXTURE_SIZE` and uniform wiring.

Do not call `audio_read()` from another analysis consumer: it advances playback.
Analyze at ingest or use a non-destructive tap/FIFO.

## Documentation

- [API](docs/api.md): current IPC reference.
- [Studio](docs/studio.md), [speech](docs/tts.md), [chat](docs/chat.md): user controls,
  API setup, keyring storage and character cues.
- [Architecture](docs/architecture.md): ownership, pipeline and event clocks.
- [Fitting/mirroring](docs/window-fitting.md): viewport policy, derivative VRMA
  creation and optional `make test-fit` rendered-pixel checks.
- [Animation properties](docs/animation-properties.md): loopability, startup, idle
  roles and manual/automatic motion ownership.
- [Motion](docs/motion.md), [foot planting](docs/foot-planting.md): algorithms/limits.
- [Locomotion](docs/locomotion.md): relative/absolute X walking, MCP jobs and completion.
- [Development](docs/development.md): build variants, assets and tests.
- [Roadmap](docs/roadmap.md): outstanding product work.
- [External-agent architecture](docs/plugins.md), [MCP](docs/mcp.md): optional
  SDK-based stdio server, private companion API and session-owned presentation.
  External harnesses own advanced tools/skills; keep built-in chat basic.
- [Packaging](docs/packaging.md): Arch PKGBUILD, staged installs, user paths and
  migration. GitHub Actions package builds are planned for later.

## Build and verification

Build with `make`; run `./domovushka --model /path/to/avatar.vrm`, or
`./vrm-overlay /path/to/avatar.vrm`. Initialize the ozz submodule before building.
`USE_OZZ=0` omits ozz sampling/transitions and leg IK; run `make clean` when changing
this feature flag. There is no Jolt dependency.

Use `make test` for headless native/Python regressions and `make test-sanitize`
for ASan/UBSan. PySide6 enables optional offscreen QML interaction coverage.
`make test-ui` needs X11/audio and local assets; `make test-speech` also needs a
prepared real voice or download access. `make test-chat` uses a local streamed
LLM fixture with the real renderer/Nano; no API credentials or paid call is needed.
Paths and reference-fixture checks are documented in the development guide.
Keep synthetic tests asset-independent.
