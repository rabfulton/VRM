# Development and testing

Start with the [build instructions](../README.md). Keep ozz-animation initialized
through `.gitmodules`; the other runtime libraries are vendored. Native builds
need GLFW/libepoxy development packages even when running only headless tests.

## Source and local data

- `src/`: C/C++ renderer and runtime.
- `vrm_overlay/`: reusable Python client, speech adapters and Qt Quick controller.
- `tests/`: maintained native, Python and QML regressions.
- `scripts/setup_speech.py`: optional native speech preparation.
- `examples/chat_tts/`: independent typed chat/TTS integration example.
- `docs/`: current user and implementation guides.
- `animation/`: local animation library; only its setup README belongs in Git.
- `build/`, `deps/_build/`: generated tests, dependency builds and speech downloads.

Avatars, animation packs, model weights, generated binaries/objects and local
settings are excluded from source changes. Keep asset notices with your local
copies and check redistribution permissions before publishing assets. Removing an
already tracked asset does not remove it from existing Git history.

## Build variants

The default uses ozz sampling, smooth transitions and leg IK. Legacy sampling can
be built without ozz:

```sh
make clean
make USE_OZZ=0 -j4
```

Run `make clean` again before returning to `make`. Object files are currently
built beside their sources, so a flag change alone does not invalidate them.
`make clean` removes runtime objects and the executable; it keeps dependency
builds, downloaded voices and local assets. The former `USE_OZZ_JOLT` option has
been replaced by `USE_OZZ`; Jolt was an unused stub and is no longer required.

## Headless regressions

```sh
make test
make test-sanitize
```

These exercise IPC transport/ownership, concurrent audio and cancellation,
springs, transitions and synthetic humanoid IK. Python tests cover client
framing, speech configuration/installers, HTTP fixtures and cancellation.
No avatar, animation pack, voice download, display or AI service is required.
Unix sockets must be available. The ozz dependency is needed for IK/transition
coverage in the default build. Sanitizer builds use `build/tests/sanitize`.

Install `requirements-gui.txt` to include offscreen QML interaction tests;
otherwise those tests skip. To check the workspace alone:

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=basic QT_QUICK_BACKEND=software python3 control.py --check
```

Use a separate `TEST_BUILD_DIR` when testing a non-default feature configuration
so objects from another build cannot be reused.

## Renderer integration checks

```sh
make test-ui
make test-speech
```

These start a real renderer with an isolated socket and temporary controller
settings. They require X11, audio output, PySide6, a local humanoid avatar with
expressions and an animation library. The controller window renders offscreen;
the avatar window opens on the desktop. The default paths are `test.vrm` and
`animation/`, but these assets are not distributed with the source.

Supply other paths with environment variables or arguments:

```sh
VRM_SMOKE_MODEL=/path/to/avatar.vrm VRM_SMOKE_ANIMATIONS=/path/to/animations make test-ui
python3 tests/qt_smoke.py --model /path/to/avatar.vrm --animation-dir /path/to/animations
```

The smoke check plays every animation, uses actual Next/Previous/blink controls,
and checks playback, expressions, tuning persistence, shaders and silent audio.
Named clips `Relax`, `restless_arms`, `push_left`, `push_right` and `Jump` enable
additional stance/jump checks when present. Generic assets report collider/contact
coverage; `--reference-fixture` also requires the original local fixture's two
supported legs, six garment chains, at least sixteen arm volumes and planted
stance coverage. Use it when reviewing physics against that fixture.

`make test-speech` additionally synthesizes real Nano on Vulkan and checks PCM
playback/drain. Prepare first with `python3 scripts/setup_speech.py --prepare`,
or permit the initial download. CPU can be selected with
`make test-speech SPEECH_DEVICE=cpu`.

## Before publishing

Run the relevant checks and `git diff --check`. Review untracked source and docs
with `git status --short`; `.gitignore` deliberately allows Markdown, text notices
and source images. Runtime downloads and your avatars should stay ignored. No
project-wide license is supplied here; choose one before presenting the project
as licensed for redistribution, and preserve third-party notices separately.
