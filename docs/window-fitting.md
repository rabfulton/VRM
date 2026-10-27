# Overlay fitting and mirrored animations

With **Fit overlay to animation** enabled in Studio's View tab, the transparent
window fits the full animation envelope rather than a fixed portrait rectangle.
Wide, displaced push poses need room relative to the camera centre, not only a
width measured between their leftmost/rightmost bones.

## Fitting policy

The loader builds compact geometry envelopes for skinned joints, including the
rendered morph-target extents. Clip sampling collects a model-space 3D envelope
across every clip in the loaded file. The camera projects that envelope, and live
geometry expands the window further for transitions and spring motion. Padding
includes the outline and the renderer's offset/blurred silhouette shadow.

Expansion preserves the screen-space camera centre and pixel density; enlarging
an ordinary viewport's height alone would magnify the avatar. The projection
compensates for that height change. The viewport can grow during playback but
only shrinks on explicit refitting, such as a clip or camera change, to avoid
breathing with each gesture. Window dimensions are limited by the monitor workarea
and a 4096 logical-pixel cap; exceptionally large projected motion is reduced to
fit those limits. Geometry crossing the camera's near plane cannot be fixed by
resizing the window.

Legacy `bounds.world_w` / `bounds.world_h` sidecar entries are retained when editing
configs but are no longer used for fitting. They were bone-only, lost the envelope's
position/depth and were incorrectly shared across avatar models and correction
settings. Bounds are recomputed in memory on animation load. Pose transforms are
restored after sampling. Live fitting follows the current camera and model transform.

Turning auto-fit off keeps manual viewport sizing available. Input hit testing
still uses the avatar's compact interaction region, rather than making the whole
expanded transparent window capture clicks.

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

```sh
python3 tests/push_fit_smoke.py --model /path/to/avatar.vrm --animation-dir /path/to/animations
```
