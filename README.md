# vrm-overlay

A lightweight Linux/X11 avatar overlay with a standalone Qt Quick controller.
The C/C++ runtime renders VRM/glTF models, plays VRMA/GLB animations, simulates
spring bones, and drives expressions and lip sync through JSON IPC.

## Build and launch

Requires a C11/C++20 toolchain, GNU Make 4.3+, CMake, pkg-config, and development
packages for GLFW, libepoxy, X11 and Xext. Python 3.10+, PySide6 and keyring are
needed for Studio and chat. The renderer itself has no Qt dependency.

```sh
git clone --recurse-submodules https://github.com/rabfulton/VRM.git
cd VRM
make -j4
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements-gui.txt
python3 control.py --model /path/to/avatar.vrm --animation-dir /path/to/animations
```

For an existing clone, initialize dependencies with
`git submodule update --init --recursive` before building. The retained submodule
is ozz-animation; other runtime libraries are vendored under `deps/`.

Supply your own avatars and animations. These assets, speech downloads and local
settings are ignored by Git. See [asset setup](animation/README.md).
Studio remembers the selected model and animation folder. On a new session it
enables blinking, plays a configured startup once, then moves to an idle.
Use **⋯** beside an animation to edit `loopable`, `startup` and `is_idle` in its
`.cfg` file; see [animation properties](docs/animation-properties.md).

Studio starts a renderer when needed. Closing the last Studio/chat window closes
only a renderer this application started.
To manage the renderer separately:

```sh
./vrm-overlay --socket /tmp/my-avatar.sock /path/to/avatar.vrm
python3 control.py --attach --socket /tmp/my-avatar.sock
```

The runtime supports one active controller connection. Disconnect Studio before
connecting another client. Use a separate socket for each independent instance.

## Capabilities

- Transparent avatar window, camera controls and toon/anime/PBR rendering.
- Animation library, playback/seek/loop controls and velocity-preserving ozz transitions.
- Fixed-step spring simulation with garment/arm collision proxies and diagnostics.
- Flat-floor foot planting with leg IK and smooth contact release.
- VRM expressions, automatic blinking and lip sync enabled by default.
- API chat with streamed replies, system-keyring credentials, sentence speech
  and library checkboxes selecting which animations the character may use.
- Local speech with Nano/CrispASR on Vulkan by default; selectable Turbo, Kokoro,
  Piper and custom CrispASR backends, with saved profiles and optional CPU processing.

In Studio's **Speech** tab, select **Prepare voice**, then try a preview. Native
engines and models download separately; no Python ML stack is required.
See [speech setup and limits](docs/tts.md). Open **Chat…**, right-click the avatar,
or press Ctrl+Enter while the app has focus to start a conversation. Chat Settings
select the API endpoint/model and save its key to your system keyring. Replies
speak sentence by sentence as they stream; see the [chat guide](docs/chat.md).
The Speech tab remains a whole-WAV voice preview.

Physics uses skeleton proxies rather than full cloth mesh contact. Foot planting
currently assumes a flat floor. Desktop window interaction, microphone input and
system-audio reactions remain planned work; see the [roadmap](docs/roadmap.md).

## Control from code

Commands are newline-delimited JSON over a Unix socket, defaulting to
`/tmp/vrm-overlay.sock`. From this checkout:

```python
from vrm_overlay import IpcClient

with IpcClient() as avatar:
    avatar.load_model("/path/to/avatar.vrm")
    avatar.auto_blink_enable(True)
    print(avatar.request({"action": "list_expressions"}))
```

See the [IPC reference](docs/api.md) for animation, expressions, streamed PCM,
state queries and tuning commands. The shared Python client consumes replies,
matches request IDs and raises `IpcError` on rejected commands.

## Documentation and checks

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
