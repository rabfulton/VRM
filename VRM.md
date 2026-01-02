# VRM Desktop Overlay Viewer (GTK3 + libepoxy) — Implementation Plan

## 1) Summary
Build a Linux **desktop-overlay VRM viewer**: a GTK3 application that opens a **frameless, transparent window** and renders/animates **VRM models** (VRM0.x / VRM1.0 where feasible). The viewer is **headless from a UX standpoint** (no built-in file chooser/UI); it is controlled by a **separate Python application** over a local **IPC protocol**.

**Primary deliverable**: a single executable (e.g., `vrm-overlay`) that:
- Creates an OpenGL 3.3 Core context via **GtkGLArea** + **libepoxy**.
- Loads `.vrm` (glTF 2.0 GLB with VRM extensions).
- Renders with **alpha transparency** so the desktop shows through.
- Plays/pauses/seeks animation; sets pose and blendshapes.
- Exposes a stable **JSON-over-UNIX-socket** control interface.

---

## 2) Goals / Non-goals

### Goals
- **Transparent overlay window** (alpha-composited) on Wayland and X11.
- **External control** via IPC (Python is the “UI”).
- **OpenGL rendering** with PBR-ish shading sufficient for VRM avatars.
- **Skinning + morph targets** (blendshapes) + animation playback.
- Deterministic, testable core with clear module boundaries.

### Non-goals (initially)
- Full, spec-complete VRM 1.0 implementation.
- SpringBone / collider physics (can be added later).
- Advanced transparency sorting (hair/eyes) beyond a basic approach.
- A full in-app editor UI.

---

## 3) Platform & Runtime Assumptions
- **Linux** only.
- **GTK 3.16+** required (for `GtkGLArea`).
- Wayland compositors generally support alpha surfaces.
- On **X11**, transparent windows require a compositor (e.g., `picom`).

---

## 4) Technology Choices

### Core
- **Language**: C99
- **GUI**: GTK3 (`gtk+-3.0`) + `GtkGLArea`
- **OpenGL loading**: `epoxy/gl.h`

### Parsing / Assets
- **glTF/GLB loader**: `tinygltf` (vendored/submodule)
- **Images**: `stb_image.h` (vendored)
- **JSON for IPC**: `cJSON` (vendored) (keeps runtime deps small)

### Math
- Prefer a small C-friendly math layer:
  - Option A: `cglm` (C library)
  - Option B: vendored minimal vec/mat/quat utilities

(Previous plan referenced `glm` which is C++; for a C99 project, `cglm` or a tiny C math layer is more consistent.)

### Build
- makefile

---

## 5) User-facing Behavior (Overlay Window)

### Window properties
- Frameless, optionally click-through (later), always-on-top/below configurable.
- Transparent background (alpha = 0 clear).
- Resizable and movable **by external controller** (Python / WM tooling).

### Compositing notes
- **Wayland**: transparency should work via GTK/Wayland.
- **X11**: require compositing manager; document requirement and provide a runtime warning if not composited.

---

## 6) IPC Control Plane

### Transport
- **UNIX domain socket** at a configurable path.
  - Default: `/tmp/vrm-overlay.sock`
  - On startup: unlink any stale socket file.

### Message framing
- **NDJSON** (one JSON object per line). This avoids length-prefixing and is simple for Python.

### Request/Response format
Each request includes an `id` so the Python client can match responses.

**Request**
```json
{"id": 1, "action": "load", "path": "/home/me/avatar.vrm"}
```

**Response**
```json
{"id": 1, "status": "ok"}
```

**Error response**
```json
{"id": 1, "status": "error", "error": {"code": "E_LOAD", "message": "Failed to parse VRM"}}
```

### Core command set (v1)
- `ping`
- `load {path}`
- `unload`
- `set_visible {visible: bool}`
- `set_window {x, y, width, height}` *(best-effort; may be limited on Wayland)*
- `set_camera {azimuth, polar, dist, target:[x,y,z]}`
- `play {speed}`
- `pause`
- `set_time {t}` *(seconds)*
- `set_animation {index}` *(select glTF animation clip)*
- `set_blendshape {name, value}` *(0..1)*
- `set_pose_bone {bone, rot:[x,y,z,w]}` *(quaternion in model space; v1 simplistic)*
- `quit`

### Threading model
- IPC runs on GLib’s main loop using `GSocketService`.
- Commands mutate shared app state; updates are **queued** onto the GTK/GL thread using `g_idle_add()`.
- Protect shared state with a `GMutex` where needed.

---

## 7) Rendering & Animation Pipeline

### OpenGL context
- Use `GtkGLArea`.
- Require OpenGL 3.3 core:
  - `gtk_gl_area_set_required_version(area, 3, 3)`

### Transparent rendering
- Clear with alpha 0:
  - `glClearColor(0,0,0,0)`
- Enable blending for materials that need it:
  - Prefer **premultiplied alpha** pipeline:
    - `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`
  - Ensure fragment output is premultiplied (or convert as needed).

### Materials (initial)
- glTF metallic-roughness PBR approximation:
  - baseColorFactor + baseColorTexture
  - metallicRoughnessTexture
  - normalTexture (optional)
- glTF `alphaMode`:
  - `OPAQUE` → normal depth write
  - `MASK` → alpha cutoff
  - `BLEND` → blended pass (no depth write or careful policy)

**Transparency policy (v1)**
- Render opaque first (depth write on).
- Render blended second (depth test on, depth write off).
- Sorting blended primitives per-mesh (not per-triangle) initially.

### Skinning
- GPU skinning in vertex shader.
- Bone matrices stored in a UBO (or SSBO where available). Target 128 bones.

### Morph targets (blendshapes)
- Support glTF morph targets:
  - weights drive deltas for position/normal (as available).
- For v1, implement either:
  - CPU accumulation into a dynamic VBO (simpler, slower), OR
  - GPU morph via additional attributes/texture buffers (faster, more complex).

Recommendation for MVP: **CPU morph** with clear performance constraints; later upgrade to GPU morph.

### Animation
- Support glTF animation channels for node TRS.
- Sample at time `t` with linear interpolation for translation/scale and slerp for rotation.
- Looping behavior controlled by IPC (`play`, `pause`, `set_time`).

---

## 8) VRM Support Scope

### VRM0.x (priority)
- Parse VRM extension metadata:
  - humanoid bone mapping
  - blendshape groups (mapped to glTF morph target weights)
  - material properties as available

### VRM1.0 (best-effort)
- Load as glTF 2.0 + `VRMC_vrm` extension where present.
- Many avatars still ship in VRM0; support both with graceful degradation.

### Bone naming
- Provide a normalized “humanoid bone name” namespace for IPC (`hips`, `spine`, `head`, etc.).
- Map to actual node indices per model.

---

## 9) Module Breakdown

### `app/` (or `src/`)
- `main.c`
  - GTK init
  - transparent window setup
  - `GtkGLArea` creation
  - tick timer / frame scheduling
  - startup of IPC server

- `ipc.c` / `ipc.h`
  - UNIX socket server
  - NDJSON parsing via cJSON
  - command dispatch → enqueue to main thread

- `renderer.c` / `renderer.h`
  - shader compilation
  - pipeline setup
  - draw passes
  - texture + buffer management

- `gltf_loader.c` / `gltf_loader.h`
  - tinygltf integration
  - buffer/texture extraction

- `vrm_parse.c` / `vrm_parse.h`
  - VRM0/VRM1 extension parsing
  - humanoid + blendshape mapping

- `animator.c` / `animator.h`
  - animation sampling
  - skeleton pose output (bone matrices)

- `camera.c` / `camera.h`
  - orbit camera and view/projection

- `math/` (if vendored)

### Shared state object
```c
typedef struct {
  // rendering
  Renderer renderer;

  // loaded content
  VRMModel *model;

  // animation
  Animator animator;

  // view
  OrbitCamera camera;

  // synchronization
  GMutex lock;

  // window/visibility state
  gboolean visible;
} VrmApp;
```

---

## 10) File Layout
```
vrm-overlay/
├── meson.build
├── src/
│   ├── main.c
│   ├── ipc.c ipc.h
│   ├── renderer.c renderer.h
│   ├── gltf_loader.c gltf_loader.h
│   ├── vrm_parse.c vrm_parse.h
│   ├── animator.c animator.h
│   ├── camera.c camera.h
│   └── shaders/
│       ├── skinned.vert
│       ├── pbr.frag
│       └── unlit.frag
├── deps/
│   ├── tinygltf/
│   ├── cjson/
│   └── stb/
├── assets/
│   └── sample.vrm
└── README.md
```

---

## 11) Implementation Roadmap

### Phase 0 — Overlay foundation (0.5–1 day)
- Transparent GTK3 window + `GtkGLArea`.
- Verify alpha compositing:
  - Wayland: confirm desktop visible through window.
  - X11: detect non-composited environment and print warning.

### Phase 1 — Rendering skeleton (1 day)
- Shader compilation, camera, basic mesh draw.
- Frame scheduling (fixed timestep or vsync-driven render).

### Phase 2 — glTF/GLB loading (2 days)
- Load GLB via tinygltf.
- Render static meshes with baseColor texture.

### Phase 3 — Skinning + animation playback (2–3 days)
- Implement skinning path (bones UBO).
- Implement glTF animation sampling and playback loop.

### Phase 4 — VRM extensions + blendshapes (2–3 days)
- Parse VRM humanoid mapping.
- Implement blendshape weight controls via IPC.
- Implement CPU morph target accumulation (MVP).

### Phase 5 — IPC completion + stability (1–2 days)
- Implement full v1 command set.
- Robust error reporting and socket lifecycle.
- Add logging + diagnostics commands (`get_state`, `list_animations`, etc.).

### Phase 6 — Quality and performance (ongoing)
- Better transparency sorting.
- Upgrade morphs to GPU.
- Optional click-through or input shaping.

---

## 12) Testing & Diagnostics

### Functional tests
- Load/unload repeatedly (memory leak checks).
- Play/pause/seek.
- Set blendshape weights.
- Switch animation clips.

### Visual tests
- Transparency correctness (clear alpha, blended materials).
- Skinning correctness (pose matches reference viewer).

### Tools
- RenderDoc for frame capture.
- `G_DEBUG=fatal-warnings` for GTK warnings.
- Optional `GL_KHR_debug` logging.

---

## 13) Python Controller Integration (example)
Minimal client sketch (NDJSON):
```python
import json, socket

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/vrm-overlay.sock')

def call(req):
    sock.sendall((json.dumps(req) + "\n").encode())
    line = sock.makefile().readline()
    return json.loads(line)

print(call({"id": 1, "action": "load", "path": "assets/sample.vrm"}))
print(call({"id": 2, "action": "play", "speed": 1.0}))
print(call({"id": 3, "action": "set_blendshape", "name": "Blink", "value": 1.0}))
```

---

## 14) Open Questions (to finalize before coding)
1. **Window behavior**: should the overlay be always-on-top, always-on-bottom, or configurable?
2. **Wayland control**: does your Python app already use a Wayland-specific method to position/resize? (Many WMs restrict it.)
3. **Blendshape naming**: do you want VRM preset names (e.g., `Blink`, `A`, `I`, `U`, `E`, `O`) or raw morph target indices?
4. **Click-through**: should the overlay ignore mouse input by default?

---

## 15) Acceptance Criteria (MVP)
- On Linux (Wayland or X11+compositor), the window background is transparent and the avatar is visible.
- Python app can:
  - load a `.vrm`,
  - start/stop animation,
  - set at least one blendshape,
  - set camera orbit parameters,
  - unload and quit cleanly.
