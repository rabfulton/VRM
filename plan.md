# VRM Desktop Overlay Viewer - Implementation Progress

## Completed

### Phase 0 — Overlay Foundation ✓
- [x] Makefile with GTK3/epoxy
- [x] Transparent GTK3 window with GtkGLArea
- [x] RGBA visual for compositing
- [x] Frameless window
- [x] Keyboard controls (q=quit, t=always-on-top)
- [x] Mouse camera orbit and zoom

### Phase 1 — Rendering Skeleton ✓
- [x] Shader compilation utilities
- [x] Camera module (orbit camera with view/projection)
- [x] Frame scheduling via tick callback

### Phase 2 — glTF/GLB Loading ✓
- [x] tinygltf integration (C++ wrapper)
- [x] Load meshes, materials, textures
- [x] Load nodes and scene hierarchy
- [x] Load skins (skeleton data)
- [x] Load animations
- [x] World matrix computation

### Phase 3 — Skinning + Animation ✓
- [x] Skinned vertex shader with bone matrices
- [x] PBR fragment shader with alpha modes
- [x] Bone matrix computation from skeleton
- [x] Opaque/transparent render passes
- [x] Animation keyframe sampling
- [x] Linear interpolation for translation/scale
- [x] Spherical interpolation (slerp) for rotation
- [x] Animation looping

### Phase 5 — IPC Server ✓
- [x] UNIX socket server
- [x] NDJSON protocol
- [x] Commands: ping, load, unload, set_visible, set_window, set_camera
- [x] Commands: play, pause, set_time, set_animation, quit
- [x] Python test client

## Remaining

### Phase 4 — VRM Extensions + Blendshapes
- [ ] VRM0.x extension parsing
- [ ] Humanoid bone mapping
- [ ] Blendshape groups
- [ ] set_blendshape IPC command

## File Structure
```
vrm-overlay/
├── Makefile
├── src/
│   ├── main.c           ✓ GTK init, window, GL area, IPC integration
│   ├── ipc.c/h          ✓ UNIX socket server, NDJSON protocol
│   ├── renderer.c/h     ✓ Skinned mesh rendering
│   ├── gltf_loader.c/h  ✓ tinygltf wrapper
│   ├── gltf_loader_impl.cpp ✓ tinygltf C++ implementation
│   ├── camera.c/h       ✓ Orbit camera
│   ├── animator.c/h     ✓ Animation sampling
│   ├── vrm_parse.c/h    TODO VRM extension parsing
│   └── shaders/
│       ├── skinned.vert ✓
│       └── pbr.frag     ✓
├── deps/
│   ├── tinygltf/        ✓
│   ├── cjson/           ✓
│   ├── stb/             ✓
│   └── cglm/            ✓
├── test_ipc.py          ✓ Python IPC test client
└── README.md
```

## Usage

### Run the overlay
```bash
./vrm-overlay
```

### Test IPC (in another terminal)
```bash
python3 test_ipc.py /path/to/model.vrm
```

### IPC Commands
```python
import json, socket
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/vrm-overlay.sock')

# Ping
sock.sendall(b'{"id":1,"action":"ping"}\n')

# Load model
sock.sendall(b'{"id":2,"action":"load","path":"/path/to/model.vrm"}\n')

# Play animation
sock.sendall(b'{"id":3,"action":"play","speed":1.0}\n')

# Pause animation
sock.sendall(b'{"id":4,"action":"pause"}\n')

# Set animation time
sock.sendall(b'{"id":5,"action":"set_time","t":0.5}\n')

# Select animation clip
sock.sendall(b'{"id":6,"action":"set_animation","index":0}\n')

# Set camera
sock.sendall(b'{"id":7,"action":"set_camera","azimuth":0,"polar":1.57,"dist":2}\n')

# Move window
sock.sendall(b'{"id":8,"action":"set_window","x":100,"y":100,"width":400,"height":600}\n')

# Quit
sock.sendall(b'{"id":9,"action":"quit"}\n')
```

## Keyboard Controls
- `q` or `Escape` - Quit
- `t` - Toggle always-on-top

## Mouse Controls
- Left-drag - Orbit camera
- Scroll - Zoom in/out
