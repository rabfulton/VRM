# domovushka studio (Qt Quick/QML)

domovushka studio is a separate Python/PySide6 application controlled through JSON IPC.
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
./domovushka --model /path/to/avatar.vrm --animation-dir /path/to/animations
```

Studio connects to the default socket, starting a renderer if none is listening.
The versioned [starter pack](../assets/starter/README.md) provides two selectable
avatars and matching libraries. The legacy local `test.vrm` is still preferred
when present; it is not distributed. A fresh installation can use the starter anime avatar.
A new renderer loads the selected model, plays a configured startup once,
then transitions to an idle and enables blinking. Studio starts hidden; pass
`--studio` to show it immediately. Missing assets/errors open it automatically.
Closing Studio or chat hides that window while the controller keeps running.
Right-click the avatar to reopen **Chat** or **Studio**, toggle **Always on top**,
or choose **Quit** to stop
the companion and controller. The overlay sets X11 utility/skip-taskbar/skip-pager
hints; the window manager controls their presentation. `--attach` never starts a
renderer; explicit **Quit** still shuts down the attached companion. Disconnect Studio before
using a different controller; the runtime currently services one connection.

Options: `--studio`, `--attach`, `--socket PATH`, `--model PATH`, `--animation-dir PATH`.
Model/library/audio choices are remembered with QSettings. Attaching to an existing
runtime preserves its current model, playback and behaviour state.

## Controls

- Avatar: **Choose / import avatar…** opens a thumbnail gallery with Discover and
  My avatars. Use the included anime companion, visit creator download pages, or
  import/drop a local VRM, GLB, glTF or ZIP. ZIPs offer a variant picker. Imported
  models are copied to user data and remain available after restart; **Use avatar**
  activates one. Browsing and importing also work while disconnected.
  **Add current avatar to library** saves an existing external avatar.
  Explicit `--animation-dir` takes precedence over a starter profile at initial
  startup. See [avatar imports](avatars.md) for formats, credits and storage.
- Library: choose a folder, search its VRMA/GLB files, click a name to preview one
  pass, or use Previous/Next. Previews work for unchecked clips too and never
  change permissions; loopable clips also preview once. Sidebar checkboxes enable
  automatic use: unchecked clips cannot serve as startup, idle, pose transitions
  or LLM actions. Choices
  persist per folder and work while disconnected. **⋯ → Available to LLM** is a
  separate permission stored in the clip's `.cfg`; both gates must be on for an
  animation to appear in the chat prompt. An **AI** label marks that permission.
  Changing access stops the active chat turn. Disabling an automatic clip pauses
  it; an automatic idle switches to another enabled idle when one is available.
  Explicit manual previews remain playable independently of that setting.
  Selection follows the animation actually loaded. **⋯** also edits looping,
  startup/idle flags, start/end poses and floor foot locking; see [animation properties](animation-properties.md).
  **Resume idle** returns control to the automatic idle chain after manual testing.
- Playback: play/pause, seek, speed, loop and internal clip selection for multi-clip files.
- Behaviours: automatic blink switch/rate/duration, automatic lip sync, audio playback/cancel,
  expression name/weight/application and clearing all expressions.
- Physics: garment/arm collisions, collider counts/contact residuals, spring and
  clothing profile parameters, reset, and save spring/seam
  tuning beside the current animation. Group transition multipliers and the garment collision policy are session controls.
- Animation: automatic foot planting with left/right contact diagnostics, ozz/legacy
  backend, coordinate correction, transition/seam times and
  per-joint-group transition time multipliers.
- View: visibility, floating window, [geometry-based animation fitting](window-fitting.md),
  shader, scale and camera. The viewport expands for wide/displaced poses while
  preserving the figure's pixel size.
  Frame timings show the last frame, slowest frame's stage breakdown, frames over
  50 ms, last clip load/bounds cost and viewport resizes. **Reset frame timings**
  clears peaks/counters after startup to help investigate later stutters.
- Speech: provider/model profiles, CPU/Vulkan processing, preparation, voice preview and cancellation.
- Chat: a compact separate conversation window with API settings, keyring credentials,
  streamed speech and character cues. See [chat](chat.md).
- Log: bounded renderer output and controller errors.

Dragging the avatar moves its overlay window. Transparent areas, including gaps
between limbs and the shadow, pass clicks to the window underneath. The hit mask
follows the rendered mesh rather than a bounding rectangle. Middle-drag orbits the camera;
scroll scales the figure. Right-click opens the **Chat / Studio / Always on top / Quit**
menu. A second right-click, an outside left-click, Escape or switching focus
dismisses it. Outside cancellation consumes the click rather than triggering an
avatar action. **Always on top** changes the avatar's window-manager floating
property; it is shared with the View tab and the renderer's **T** shortcut. The
renderer saves the on/off choice immediately in its XDG config file
(`~/.config/domovushka/renderer.cfg` by default) and restores it
at startup, including standalone launches. **Chat** opens chat near the pointer. **Chat…** in
Studio or Ctrl+Enter while this app has focus also opens chat; Escape hides it.
The menu uses Qt cursor/workarea coordinates so native X11 pixels do not displace
it on scaled displays; it only shifts when necessary to stay inside the workarea.
When a startup animation is set, nothing is shown before that animation begins.

## Structure and ownership

- `domovushka` / `vrm_overlay/gui.py`: launcher and QML engine lifecycle;
  `control.py` is a compatibility entry point.
- `vrm_overlay/qml/Main.qml`: layout and control presentation.
- `vrm_overlay/qml/OverlayMenu.qml`: independent popup usable with Studio hidden.
- `vrm_overlay/qml/AvatarImportDialog.qml`, `qt_avatar_library.py`, `avatar_library.py`:
  visual gallery, background imports and persistent local model installs.
- `vrm_overlay/qml/TuningSlider.qml`: reusable slider with confirmed-state binding.
- `vrm_overlay/qt_controller.py`: QObject properties/slots, QLocalSocket framing,
  ordered command transactions, timeouts, QProcess ownership and state polling.
- `vrm_overlay/qt_chat.py`, `qt_chat_network.py`, `chat.py`, `credentials.py`: streamed
  API chat, character cues, speech coordination and OS credential storage.
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
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=basic QT_QUICK_BACKEND=software ./domovushka --check
```

The chat window shares Studio's IPC transport and selected speech profile. Its
settings hold the character description and an automatically generated capability
prompt. The older Tkinter example remains an independent integration prototype;
Studio is the maintained chat interface.

Motion diagnostics and interpretation are documented in [motion behaviour](motion.md). Group factors
now multiply transition time; they no longer mask destination animation influence.
Transition progress is shown above the timeline, and the default is 0.45 seconds.

Lip sync is enabled by default and has no Studio switch. Foot planting is also
enabled by default; its Animation-tab switch is available for A/B review. See
[foot planting](foot-planting.md) for floor calibration, contact/release rules and current limitations.
