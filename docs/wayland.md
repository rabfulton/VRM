# Native Wayland support

The renderer now has two implementations behind the platform interface:
GLFW/X11 and libwayland-client/EGL/layer-shell. The default build includes both.
The native backend requires a compositor exposing `wlr-layer-shell`; ordinary
Wayland toplevels cannot provide this application's overlay placement contract.

```sh
make -j4
./domovushka --window-backend wayland --studio
```

`--window-backend auto` (default) tries native Wayland when `WAYLAND_DISPLAY` is
set, then reports an X11 fallback if native creation fails. Explicit `wayland`
and `x11` selections never fall back. `VRM_WINDOW_BACKEND` overrides only auto.
The renderer accepts the same option. `make WINDOW_BACKEND=x11` omits native
Wayland dependencies; `make WINDOW_BACKEND=wayland` omits GLFW/X11/Xext dependencies.
Each build configuration has separate object files.

## Presentation and controls

The native avatar is a transparent layer named `vrm-overlay`, not a decorated
or tiled application window. It reserves no desktop space and normally requests
no keyboard input. The avatar, animation, shaders and speech pipeline are shared
with X11. The existing bounded MSAA framebuffer resolves into an output-sized
transparent canvas; hit readbacks remain bounded to the avatar viewport.

Only the avatar stencil silhouette receives pointer input. Its logical bitmap
is converted into vertically merged rectangles, preserving holes and excluding
shadow-only pixels. Input-region changes commit with presentation. Left drag
moves the avatar within the selected output, middle drag orbits, and wheel input
zooms. Native frame callbacks pace presentation while IPC and simulation continue
when a frame is unavailable. Hidden avatars retain an empty transparent surface
and do not request ongoing frames.

Right-click opens a renderer-owned `xdg_popup` associated with the layer surface,
using the actual Wayland button serial and pointer location. Chat and Studio actions
travel over the existing IPC state stream; Quit exits the companion. Popup grab,
outside dismissal and Escape are handled in the renderer. Studio/Chat remain
ordinary Qt windows, with compositor-controlled placement and focus. The X11 QML
menu remains available for the X11 backend.

Wayland viewport coordinates are output-local. `wayland_pos_x/y` are stored
separately from X11 `window_pos_x/y` in settings.cfg; switching backends preserves
both placements. No desktop configuration or compositor rules are installed.

## Current scope

This is an initial native implementation, not completion of [the port plan](../wayland.md).
It uses one compositor-selected output with compositor-preferred integer scaling
(wl_surface v6), or scale 1 on older compositors. Fractional-scale negotiation,
explicit output selection/migration, output-identity placement, fullscreen-aware
visibility, and activation-token handoff to Qt are still outstanding. The compositor
may deny raising/focusing Studio or Chat. Multi-monitor/hotplug, suspend and lock
behaviour still need validation. The menu uses a small Cairo-rendered surface;
full theme/accessibility and keyboard navigation beyond Escape remain future work.

The output canvas requires single-sample swapchain buffers: at 5120×2160 each
RGBA buffer is approximately 42 MiB. It does not allocate full-output MSAA or
stencil readbacks. Setting Always on top off selects the bottom layer, which may
be obscured by desktop surfaces depending on the compositor.

## Validation so far

On Hyprland 0.56.2 / NVIDIA 615.78.08 at 5120×2160, scale 1, the combined build
loaded the bundled avatar in native mode with DISPLAY unset and in forced X11
mode. Both produced 48,565 visible pixels in a bounded 578×550 framebuffer,
responded to visibility changes, and exited through IPC. Native Qt startup passed.
The X11-only, Wayland-only and combined configurations all built successfully.
Focused input-region coverage, independent placement persistence, and controller
menu/Studio request checks passed. Initial manual feedback reported the native app working; detailed interaction and
multi-output acceptance remains pending. `tests/window_backend_smoke.py` repeats
the focused backend check without modifying user settings.
