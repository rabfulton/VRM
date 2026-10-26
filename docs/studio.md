# VRM Studio (Qt Quick/QML)

VRM Studio is a separate Python/PySide6 application controlled through JSON IPC.
The renderer owns a single transparent avatar window.

The Speech tab selects Nano (default), Turbo, Kokoro or Piper, saves separate
provider/model profiles, prepares native engines/models, and previews speech with
automatic lip sync. `qt_speech.py` runs synthesis outside the event loop and
shares Studio's existing IPC transport for bounded playback. See [local speech](tts.md).
Vulkan is the default processing choice; CPU remains explicitly selectable.

## Run

```sh
make
python3 -m pip install -r requirements-gui.txt
python3 control.py --model /path/to/avatar.vrm --animation-dir /path/to/animations
```

Studio connects to the default socket, starting a renderer if none is listening.
Supply your own assets; the legacy local default `test.vrm` is not distributed.
A new renderer loads the selected model, plays `Relax.vrma`
when available, and enables blinking. Closing Studio stops only a renderer it
started. `--attach` never starts or stops a renderer. Disconnect Studio before
using a different controller; the runtime currently services one connection.

Options: `--attach`, `--socket PATH`, `--model PATH`, `--animation-dir PATH`.
Model/library/audio choices are remembered with QSettings. Attaching to an existing
runtime preserves its current model, playback and behaviour state.

## Controls

- Avatar: load a VRM/GLB/glTF file through the file picker.
- Library: choose a folder, search its VRMA/GLB files, click a row, or use Previous/Next.
  Selection follows the animation actually loaded; the list scrolls to that row.
- Playback: play/pause, seek, speed, loop and internal clip selection for multi-clip files.
- Behaviours: automatic blink switch/rate/duration, automatic lip sync, audio playback/cancel,
  expression name/weight/application and clearing all expressions.
- Physics: garment/arm collisions, collider counts/contact residuals, spring and
  clothing profile parameters, reset, and save spring/seam
  tuning beside the current animation. Group transition multipliers and the garment collision policy are session controls.
- Animation: automatic foot planting with left/right contact diagnostics, ozz/legacy
  backend, coordinate correction, transition/seam times and
  per-joint-group transition time multipliers.
- View: visibility, floating window, animation bounds fitting, shader, scale and camera.
- Speech: provider/model profiles, CPU/Vulkan processing, preparation, voice preview and cancellation.
- Log: bounded renderer output and controller errors.

Dragging the avatar moves its overlay window. Middle-drag orbits the camera;
scroll scales the figure.

## Structure and ownership

- `control.py` / `vrm_overlay/gui.py`: entry point and QML engine lifecycle.
- `vrm_overlay/qml/Main.qml`: layout and control presentation.
- `vrm_overlay/qml/TuningSlider.qml`: reusable slider with confirmed-state binding.
- `vrm_overlay/qt_controller.py`: QObject properties/slots, QLocalSocket framing,
  ordered command transactions, timeouts, QProcess ownership and state polling.
- `src/main.c`: runtime commands and authoritative `get_state` snapshot.

The GUI never waits synchronously for network replies. Load-animation, blend and
play form one ordered transaction; an error aborts its remaining commands. Slow
loads have a longer deadline. Timed-out commands are not replayed. Slider changes
are coalesced and flushed before a save or animation switch. Polling updates the
UI without firing user-only switch/slider signals or sending those values back.

## Checks

`make test` includes optional offscreen QML tests when PySide6 is installed. These
click the actual Next/Previous/blink controls, check transaction failure/reconnect,
and verify tuning coalescing. `make test-ui` uses the real renderer and every local
animation file; it also exercises playback, blink, expression, spring, shader and
silent audio controls. It requires X11, playback output and local assets; see [test setup](development.md).

To check QML loading alone without a display or renderer:

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=basic QT_QUICK_BACKEND=software ./control.py --check
```

The chat/TTS Tkinter example remains an independent integration prototype; it is
not part of Studio. Conversation controls can move into QML in a later milestone.

Motion diagnostics and interpretation are documented in [motion behaviour](motion.md). Group factors
now multiply transition time; they no longer mask destination animation influence.
Transition progress is shown above the timeline, and the default is 0.45 seconds.

Lip sync is enabled by default and has no Studio switch. Foot planting is also
enabled by default; its Animation-tab switch is available for A/B review. See
[foot planting](foot-planting.md) for floor calibration, contact/release rules and current limitations.
