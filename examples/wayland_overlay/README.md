# Native Wayland overlay probe

This is the first native presentation experiment from [wayland.md](../../wayland.md),
not a replacement for the avatar renderer. It uses libwayland-client, layer-shell
and desktop OpenGL 3.3 through EGL. It has no GLFW, X11, Python or avatar dependency.
Studio continues to use Qt's native Wayland support.

Build with the system development packages for `wayland`, `wayland-protocols`,
`libepoxy` and EGL (Mesa or the GPU vendor implementation):

```sh
make wayland-probe
env -u DISPLAY build/wayland-probe/overlay --frames 240
```

A translucent cyan 160×160 logical-pixel marker appears at (100, 100) on the
compositor-selected output. It exits after 240 presented frames. To inspect it
longer or change its position:

```sh
build/wayland-probe/overlay --frames 0 --x 500 --y 250
```

Ctrl+C terminates it. Every pixel, including the marker, passes pointer input
through. The surface requests no keyboard focus, reserves no desktop space,
and uses the `top` layer with namespace `vrm-overlay-probe`. There is no taskbar
window or normal toplevel positioning request. No compositor rules are installed.

The output-sized canvas is cleared to transparent on each presentation. The
marker uses premultiplied alpha. Acknowledged configure dimensions precede EGL
buffer creation/resizing; frame callbacks pace rendering. SIGINT/SIGTERM and
compositor close/disconnect release EGL and Wayland resources. A compositor that
withholds frame callbacks can delay the frame-count exit; Ctrl+C still works.

This probe supports compositor-preferred integer buffer scales on wl_surface v6,
with scale 1 on older compositors (wl_compositor v3 minimum). Fractional scaling,
explicit output selection/hotplug migration, avatar rendering, dragging, silhouette
input regions, popup menus, IPC and fullscreen-application visibility policy remain
for subsequent port stages. Do not interpret a successful probe as interaction
parity. At 5120×2160 one RGBA buffer is about 42 MiB; swapchain buffers cost more.
The probe does not allocate output-sized MSAA, depth or GPU readback buffers.

## Host observation, 2026-10-27

Hyprland 0.56.2, NVIDIA 615.78.08, 5120×2160 at scale 1: the compositor configured
the full logical canvas and EGL supplied NVIDIA desktop OpenGL 3.3. The probe
presented 120 frames and exited successfully; a second 60-frame run succeeded
with `DISPLAY` unset. Its linked libraries contain neither GLFW nor X11. The
repaired X11/XWayland renderer loaded the bundled anime avatar, answered
`get_state`, and exited successfully through IPC `shutdown`. Native Qt startup
also passed:

```sh
QT_QPA_PLATFORM=wayland ./domovushka --check
```

These checks establish startup/presentation, not manual click-through, mixed-DPI,
lock-screen, suspend, unplug or popup acceptance. Those remain explicit gates in
the port plan.
