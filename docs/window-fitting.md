# Overlay fitting and mirrored animations

With **Fit overlay to animation** enabled in Studio's View tab, the transparent
window fits the full animation envelope rather than a fixed portrait rectangle.
Wide, displaced push poses need room relative to the camera centre, not only a
width measured between their leftmost/rightmost bones.

## Fitting policy

Wheel input is combined once per rendered frame. Zooming keeps the viewport at
its largest required size, with spare room reserved for further zoom-in, so
zoom-out changes the figure without repeatedly resizing the compositor surface.
Explicit camera/refit changes can reclaim that space. Native moves and resizes
use one request; framebuffer allocation and projection are reconciled against
the actual window size immediately before drawing.

The loader builds compact geometry envelopes for skinned joints, including the
rendered morph-target extents. Clip sampling collects a model-space 3D envelope
across every clip in the loaded file. The camera projects that envelope, and live
geometry expands the window further for transitions and spring motion. Padding
includes the outline and the renderer's offset/blurred silhouette shadow.

Expansion preserves the screen-space camera centre and pixel density; enlarging
an ordinary viewport's height alone would magnify the avatar. The projection
compensates for that height change. The viewport can grow during playback but
only shrinks on explicit refitting, such as a camera change, to avoid
breathing with each gesture. Clip switches preserve the viewport; live expansion
uses 64-pixel steps to avoid reallocating render surfaces for small spring changes.
Window dimensions are limited by the monitor workarea
and a 4096 logical-pixel cap; exceptionally large projected motion is reduced to
fit those limits. Geometry crossing the camera's near plane cannot be fixed by
resizing the window.

Legacy `bounds.world_w` / `bounds.world_h` sidecar entries are retained when editing
configs but are no longer used for fitting. They were bone-only, lost the envelope's
position/depth and were incorrectly shared across avatar models and correction
settings. An in-memory cache holds up to 32 clip envelopes for the current avatar,
keyed by path, file size/nanosecond mtime and correction quaternion. Avatar reloads
clear it. Revisiting an unchanged clip avoids sampling its envelope again. Pose transforms are
restored after sampling. Live fitting follows the current camera and model transform.

Turning auto-fit off keeps manual viewport sizing available. Input follows the
rendered avatar's silhouette, independently of the expanded viewport. The existing
skinning/morph/material passes write a stencil coverage mask after the shadow
pass. Fully transparent fragments are discarded; shadow-only pixels, background
and gaps between limbs do not capture clicks. Outlines remain interactive.
The stencil is resolved and transferred through two fenced pixel buffers at up
to 60 Hz. Only completed transfers are mapped, avoiding a synchronous GPU wait.
The resulting bitmap sets the X11 input shape and supplies mouse hit testing.
It can trail the visible pose by a frame or two. Resizing recentres the last
silhouette until the new GPU readback arrives; hiding, unloading and pending
startup clear coverage immediately. An empty figure is click-through.
Framebuffer/window scaling, fractional scaling and the bottom-left OpenGL origin
are accounted for when producing the top-left X11 bitmap.

## Create a mirrored push

```sh
python3 scripts/mirror_animation.py animation/push_right.vrma animation/push_left.vrma
```

The script creates a new animation-only VRMA and sidecar without changing or
overwriting the source. It reflects spatial transforms/keyframes across X,
conjugates quaternion rotations and swaps left/right humanoid mapping (including
hands/fingers/eyes). Cubic rotation/translation tangents are reflected as well.
It does not use a negative render scale, so winding and lighting stay normal.

Expression-control nodes keep their scalar strengths rather than having their X
value negated. Shared output accessors/samplers are copied before transformation.
Spring/seam tuning and sidecar comments are retained; obsolete bounds are discarded.
The new clip defaults to `loopable=0`, `startup=0`, `is_idle=0` and remains unchecked
for LLM access until you enable it in Studio.

The tool supports a single embedded binary buffer with ordinary float animation
accessors. Mesh/skin-bearing models, sparse outputs and compressed/external buffers
are rejected explicitly; export an animation-only VRMA first. Preserve any source
asset notices with the local derivative. Existing output files/configs are never
overwritten.

After generation, use library **Refresh** to discover the clip. Use its **⋯** button
to adjust [animation properties](animation-properties.md).

## Verification

`make test` covers asymmetric viewport projection, pixel-stable expansion, display
limits, skinned bounds, all-clip sampling/pose restoration, mirrored root/rotation,
shared accessors, expression controls and double-mirror equivalence. Sanitizer
coverage includes the native bounds code.

`make test-fit` is an optional real OpenGL check requiring a display, local avatar,
`push_right.vrma` and `push_left.vrma`. It samples 21 poses of each clip, camera orbit,
transitions and live springs. The read-only `get_frame_bounds` IPC diagnostic
resolves the last rendered MSAA frame and reports visible alpha extents and edge
pixels, so checks do not depend on desktop window occlusion. A short wait handles
an empty buffer immediately after a resize.

`make test-render` checks 24 actual MSAA frames at changing viewport sizes using
a synthetic triangle, with no avatar assets. Shadow-target allocation must restore
the caller's framebuffer; otherwise a resize renders to the default target and the
subsequent MSAA resolve overwrites it with a blank frame. This caused resize flicker.
The check also keeps avatar input active across asynchronous hit-buffer resizes.

`make test-zoom` sends real X11 wheel events to an isolated bundled avatar and
checks visible window samples, delivered zoom steps and reuse of the native
surface on zoom-out. Use `make test-zoom ZOOM_MODEL=test2.vrm` for a local avatar.
It requires PySide6, xdotool and a display; settings and sockets are temporary.

Studio's View tab exposes frame/load timings and a counter reset. The slowest
frame's IPC/update/render/swap breakdown helps distinguish synchronous clip
loading from steady rendering stalls. Render timings measure CPU submission;
swap time can include GPU completion and v-sync/compositor waits. Cold clip loading
and ozz conversion still run on the render thread and may briefly stall playback;
the bounds cache reduces repeated work but does not make loading asynchronous.

```sh
python3 tests/push_fit_smoke.py --model /path/to/avatar.vrm --animation-dir /path/to/animations
```
