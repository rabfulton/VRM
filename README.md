# domovushka

![domovushka Studio beside a transparent avatar overlay on a Wayland desktop](screenshots/screenshot.png)

An animated desktop companion for Linux/X11 and Wayland, with a Qt Quick control window,
**domovushka studio**.
The C/C++ runtime renders VRM/glTF models, plays VRMA/GLB animations, simulates
spring bones, and drives expressions and lip sync through JSON IPC.

## Arch package

To build a test package from this checkout, including uncommitted changes:

```sh
python3 scripts/build_arch_package.py -- --syncdeps
sudo pacman -U build/packages/domovushka-git-r*.pkg.tar.zst
domovushka --studio
```

This needs `base-devel` and initialized ozz sources. Copy the main package to
other up-to-date x86_64 Arch machines and install with `pacman -U`. Speech engines
and models remain optional downloads. The [PKGBUILD](packaging/arch/PKGBUILD)
builds both native Wayland and X11 support. See [packaging and user file locations](docs/packaging.md)
for dependencies, migration, staged installs and direct makepkg builds.

## Build and launch

Requires a C11/C++20 toolchain, GNU Make 4.3+, CMake, pkg-config, and development
packages for libepoxy, GLFW, X11 and Xext, plus wayland-client, wayland-egl,
EGL, Cairo and wayland-protocols for the native backend. Python 3.10+, PySide6 and keyring are
needed for Studio and chat. The renderer itself has no Qt dependency.

```sh
git clone --recurse-submodules https://github.com/rabfulton/VRM.git
cd VRM
make -j4
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements-gui.txt
./domovushka --studio
```

For an existing clone, initialize dependencies with
`git submodule update --init --recursive` before building. The retained submodule
is ozz-animation; other runtime libraries are vendored under `deps/`.

The [CC0 starter pack](assets/starter/README.md) includes an anime avatar, a fox,
14 humanoid motions and seven fox animations. Open **Choose / import avatar…**
in Studio for a thumbnail gallery, the included anime avatar and creator download
pages. Import a downloaded VRM, GLB, glTF or ZIP; **My avatars** keeps local copies
between sessions. See [avatar imports](docs/avatars.md). A fresh clone can run with
`./domovushka --studio`; existing saved/local avatars are preserved.
Additional user avatars, animations, speech downloads and local settings stay
ignored by Git. See [asset setup](animation/README.md).
The `domovushka` launcher uses this checkout's `.venv` when present, otherwise
the current Python interpreter. Run `./domovushka` for just the companion, or
`./domovushka --studio` to open the controls. All existing command-line options
are supported, and `control.py` remains a compatibility entry point. Saved
preferences, imported avatars and keyring credentials are retained.

## X11 and Wayland

The default build includes both window backends and chooses one at runtime.
On a Wayland session, it tries a native transparent layer-shell overlay when
`WAYLAND_DISPLAY` is set. If the compositor cannot provide it, the renderer
reports a fallback to X11/XWayland. Studio and Chat use Qt's platform selection
independently.

```sh
./domovushka --studio                         # Automatically select the backend
./domovushka --window-backend wayland --studio # Require native Wayland
./domovushka --window-backend x11 --studio     # Require X11 or XWayland
```

Explicit backend selections never fall back. The renderer accepts the same
`--window-backend` option; `VRM_WINDOW_BACKEND=x11` or `wayland` overrides automatic
selection. To build only one backend, use `make WINDOW_BACKEND=x11` or
`make WINDOW_BACKEND=wayland`. The Wayland-only build has no direct GLFW, X11 or
Xext dependency; build objects are kept separate for each configuration.

Native Wayland support currently targets compositors with `wlr-layer-shell` and
has been exercised on Hyprland. The avatar appears on a transparent canvas with
input restricted to its silhouette: gaps and shadows stay click-through. Left-drag
to move the avatar within its output, middle-drag to orbit, and scroll to zoom.
Right-click opens the native [radial gem menu](docs/gem-menu.md) at the pointer.

This first native implementation uses one compositor-selected output with integer
scaling. Fractional scaling, explicit output selection and migration, and
fullscreen-aware visibility are still pending. The compositor controls placement
and focus for Studio and Chat. See [native Wayland support](docs/wayland.md) for
current behavior, validation and limits.

## Companion controls

The package installs a desktop entry and the `domovushka` command on `PATH`.

Studio remembers the selected model and animation folder. On a new session it
enables blinking, plays a configured startup once, then moves to an idle.
Use **⋯** beside an animation to edit looping, startup, idle, pose and floor-contact properties in its
`.cfg` file; see [animation properties](docs/animation-properties.md).

The companion starts with Studio hidden. Right-click for the avatar-coloured
gem menu: **Talk to me**, **Studio**, **Always on top**, **Hide avatar**, **Colours**
and **Quit**. Both desktop backends use the same menu; rendering
preferences remain available in Studio. The floating preference is remembered
across restarts. Right-click again or left-click outside to dismiss the menu.
Closing Studio or chat keeps the companion running;
**Quit** stops it. Pass `--studio` to open the controls immediately. Missing assets
or errors open Studio for setup and recovery. The avatar requests exclusion from
X11 taskbars and window lists. Animations marked `is_idle` take turns while the
companion is free; use **Resume idle** after manual playback or testing.
To manage the renderer separately:

```sh
./vrm-overlay --socket /tmp/my-avatar.sock /path/to/avatar.vrm
./domovushka --attach --socket /tmp/my-avatar.sock
```

The runtime supports one active controller connection. Use **Log → Disconnect controller** in Studio before
connecting another client. Use a separate socket for each independent instance.

## Capabilities

- Transparent avatar window, camera controls and toon/anime/PBR rendering.
- Animation library, playback/seek/loop controls and velocity-preserving ozz transitions.
- Fixed-step spring simulation with garment/arm collision proxies and diagnostics.
- Flat-floor foot planting with leg IK and smooth contact release.
- VRM expressions, automatic blinking and lip sync enabled by default.
- API chat with streamed replies, system-keyring credentials, sentence speech,
  animation master switches and separate per-animation LLM permissions.
- Local speech with Nano/CrispASR on Vulkan by default; selectable Turbo, Kokoro,
  Piper and custom CrispASR backends, with saved profiles and optional CPU processing.

In Studio's **Speech** tab, select **Prepare voice**, then try a preview. Native
engines and models download separately; no Python ML stack is required.
See [speech setup and limits](docs/tts.md). Open **Chat…**, choose **Talk to me** from the avatar's right-click menu,
or press Ctrl+Enter while the app has focus to start a conversation. Chat Settings
select the API endpoint/model and save its key to your system keyring. Replies
speak sentence by sentence as they stream; see the [chat guide](docs/chat.md).
The Speech tab remains a whole-WAV voice preview.

Physics uses skeleton proxies rather than full cloth mesh contact. Foot planting
currently assumes a flat floor. Arch packages are ready for installation testing;
optional user-authored agent skills, desktop guidance, window interaction, microphone input
and system-audio reactions remain planned work. Maintained desktop targets are
Hyprland and Xorg; other compositor integrations are welcome as contributed PRs.
See the [roadmap](docs/roadmap.md) and [packaging guide](docs/packaging.md).

## Control from code

Advanced users can use an external agent’s own chat, tools and skills while the
avatar presents its responses. Select **Speech → Conversation → External agent (MCP)** in
Studio, or launch with `domovushka --external-agent --studio`. Install the optional
`python-mcp` package on Arch (or `requirements-mcp.txt` in a checkout environment),
then configure your agent to run `domovushka-mcp` as a stdio server. The built-in
chat remains the basic standalone option. See [MCP setup and tools](docs/mcp.md).

Hide/summon is available through a global shortcut, command line and MCP; the
avatar's right-click menu also offers Hide. Set a shortcut in Studio's header
for Xorg or Hyprland, and mark an animation
**Play before hiding** for an optional exit. Summoning replays the entrance.
See [visibility and shortcut setup](docs/studio.md#hiding-and-summoning).

Commands are newline-delimited JSON over a private Unix socket, defaulting to
`$XDG_RUNTIME_DIR/domovushka/renderer.sock` (or `/tmp/domovushka-UID/renderer.sock`
without a valid runtime directory). The Python client resolves this automatically:

```python
from vrm_overlay import IpcClient

with IpcClient() as avatar:
    avatar.load_model("/path/to/avatar.vrm")
    avatar.auto_blink_enable(True)
    print(avatar.request({"action": "list_expressions"}))
```

The project is [MIT licensed](LICENSE); bundled components and starter assets
retain their own notices. See the [IPC reference](docs/api.md) for animation, expressions, streamed PCM,
state queries and tuning commands. The shared Python client consumes replies,
matches request IDs and raises `IpcError` on rejected commands.

## Documentation and checks

- [Native Wayland support](docs/wayland.md)
- [Studio guide](docs/studio.md)
- [API chat and character actions](docs/chat.md)
- [Local speech](docs/tts.md)
- [Window fitting and mirrored animations](docs/window-fitting.md)
- [Motion and clothing contacts](docs/motion.md)
- [Foot planting](docs/foot-planting.md)
- [Architecture and timing](docs/architecture.md)
- [Development and testing](docs/development.md)

```sh
make test            # Headless native/Python regressions; no model or AI service
make test-sanitize   # Native checks with ASan/UBSan
make test-ui         # Real renderer and local reference assets; X11/audio required
make test-speech     # Also synthesize real Nano on Vulkan; downloads may be needed
make test-chat       # Streamed local LLM fixture + real renderer/Nano; no API bill
make test-fit        # Rendered-pixel clipping check for both push animations
```

When PySide6 is installed, the headless suite also tests actual QML controls
with offscreen rendering. Integration asset requirements and path overrides are
in the [development guide](docs/development.md).
