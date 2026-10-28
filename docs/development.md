# Development and testing

Start with the [build instructions](../README.md). Keep ozz-animation initialized
through `.gitmodules`; the other runtime libraries are vendored. The default
combined build needs the X11 and Wayland development dependencies listed in the
README. Backend-specific builds omit the other backend's direct dependencies.
Arch install/package builds are documented in the [packaging guide](packaging.md).
`scripts/build_arch_package.py` tests the current working tree through the same
PKGBUILD. GitHub Actions builds are planned for later.

## Source and local data

- `src/`: C/C++ renderer and runtime.
- `vrm_overlay/`: reusable Python client, speech adapters and Qt Quick controller.
- `tests/`: maintained native, Python and QML regressions.
- `scripts/setup_speech.py`: optional native speech preparation.
- `examples/chat_tts/`: independent typed chat/TTS integration example.
- `docs/`: current user and implementation guides.
- `animation/`: local animation library; only its setup README belongs in Git.
- `build/`, `deps/_build/`: generated tests, dependency builds and speech downloads.

The audited [CC0 starter pack](../assets/starter/README.md) is versioned separately
from user assets, with a reproducible build recipe and licence/provenance records.
Additional user avatars, animation packs, model weights, generated binaries/objects
and local settings are excluded from source changes. Keep asset notices with your local
copies and check redistribution permissions before publishing assets. Removing an
already tracked asset does not remove it from existing Git history.

## Build variants

The default uses ozz sampling, smooth transitions and leg IK. Legacy sampling can
be built without ozz:

```sh
make clean
make USE_OZZ=0 -j4
```

Run `make clean` again before returning to `make`. Runtime object files live under `build/obj/<backend>-ozz<flag>/`, so backend
and ozz configurations cannot reuse incompatible objects.
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
No extra user-supplied avatar/animation pack, voice download, display or AI service
is required. The versioned starter assets are checked against their manifest.
Unix sockets and local loopback HTTP sockets must be available. Chat tests use
local SSE fixtures, fake voices and fake keyrings; they never access real credentials. The ozz dependency is needed for IK/transition
coverage in the default build. Sanitizer builds use `build/tests/sanitize`.

Install `requirements-gui.txt` to include offscreen QML interaction tests;
otherwise those tests skip. To check the workspace alone:

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=basic QT_QUICK_BACKEND=software ./domovushka --check
```

Use a separate `TEST_BUILD_DIR` when testing a non-default feature configuration
so objects from another build cannot be reused.

## Renderer integration checks

```sh
make test-ui
make test-speech
make test-chat
make test-render
make test-desktop
make test-springs
make test-zoom
make test-assets
```

`test-render` requires X11/OpenGL but no local assets: it checks that shadow
reallocation preserves the MSAA target and all 24 synthetic frames contain the
test triangle. `test-desktop` additionally requires PySide6, `xdotool`, `xprop`, a
local avatar and two VRMA clips. It uses a temporary library/settings/socket and
renderer, exercises idle rotation and bounds reuse, checks X11 window hints,
right-clicks the avatar, opens/closes Chat and Studio, then chooses Quit. It restores
the pointer afterward. Asset paths can be supplied to `tests/desktop_smoke.py`.
The GL check also verifies that figure coverage excludes shadow/fully transparent
fragments. Desktop checks verify blank startup setup frames, clicks reaching a
test window through background/interior gaps, and exact Qt cursor menu placement.
Run with `QT_SCALE_FACTOR=2 make test-desktop` to exercise another Qt scale.

`test-assets` needs X11/audio and uses only bundled assets, an isolated socket and
temporary renderer settings. It samples every starter motion, checks visible
unclipped pixels, and verifies that each clip's floor-contact policy is applied.
The manifest test verifies source/output hashes and embedded texture references.
To regenerate assets, use `python3 scripts/build_starter_assets.py --download`.
See the starter README for supported source rigs and visual-preview commands.

The UI/speech/chat checks start a real renderer with an isolated socket and temporary controller
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
and source images. Runtime downloads and your avatars should stay ignored. The
project uses [MIT](../LICENSE); dependencies and assets retain their separate
licences, and installed packages preserve their notices.

`make test-chat` uses the same local assets with a streamed HTTP fixture, real
Nano speech and QML chat controls. It verifies expression and thinking/idle cues
without an API key or paid LLM request. The local animation folder must include
`Thinking.vrma` and `Relax.vrma` for that gesture scenario.

`make test-fit` checks actual alpha edges for wide push poses and their mirrored
variant. See [window fitting and mirroring](window-fitting.md) for local asset
requirements and the dependency-free mirroring script.

## Window backends

The default `WINDOW_BACKEND=both` build chooses at runtime. `x11` and `wayland`
builds omit the other backend and its direct dependencies. `--window-backend`
(and `VRM_WINDOW_BACKEND` when selection is auto) controls runtime selection.
See [Wayland](wayland.md) for the native layer-shell contract and limitations.
Focused real-display checks use isolated settings and the bundled avatar:

```sh
python3 tests/window_backend_smoke.py --backend wayland --auto
python3 tests/window_backend_smoke.py --backend x11
```

The native check removes `DISPLAY`; the X11 check removes `WAYLAND_DISPLAY`.
Both inspect rendered avatar pixels and exercise visibility and IPC shutdown.
They do not inject pointer input or assert popup interaction. Existing X11 desktop
smokes should explicitly run with `VRM_WINDOW_BACKEND=x11`.
