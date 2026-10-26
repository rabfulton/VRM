# Foot planting and leg IK

The runtime now implements flat-floor foot planting in `src/foot_lock.cpp`, using
`ozz::animation::IKTwoBoneJob`. It runs after animation sampling/blending and before
secondary motion. The solver changes leg rotations and a bounded pelvis translation;
it does not stretch bones. The optional ozz build must be enabled, but either the
ozz or legacy animation sampler can supply the input pose.

## Contact ownership

Each mapped upper-leg/lower-leg/foot chain has independent contact history, a
model-space ankle anchor, an orientation anchor and an influence weight. Contact
classification uses the **unblended target** animation, so motion introduced by a
transition is not mistaken for an authored walking step. The IK correction is then
applied to the displayed, blended pose. Transition pose/velocity history is recorded
**before IK**. Direct transitions, interruptions and reload snapshots all use this
animation-layer history; the persistent contact layer is applied once afterward.
Recording the corrected pose would apply pelvis/leg corrections twice on restart.
Pausing clears source velocity without replacing that history with the corrected pose.

A foot acquires a contact after roughly 60 ms of low motion near its floor plane.
Its influence ramps in over 80 ms. It releases over 120 ms when the target lifts,
steps, turns beyond the orientation tolerance, or becomes unreachable. A modest
pelvis drop keeps nearly straight legs within reach; this correction is bounded
to 18% of leg length and follows a critically damped response rather than changing
immediately with reach requirements. Knee direction comes from the **blended** pose,
retaining the previous plane near a straight leg. Both leg rotations and ankle
orientation fade with contact influence; interpolating only the target position
would still apply full knee-plane corrections at tiny weights. Anchored ankle orientation is preserved as well as
position. Limbs with missing mappings, zero length, nonuniform/sheared/reflected
transforms or spring-bone ownership are skipped rather than distorted.

The floor plane comes from each ankle's bind-pose height. At clip entry, a bounded
constant height offset is inferred from the lower target foot to accommodate
retargeted clips that float slightly above the model's bind floor. The physical
anchor plane stays fixed. The allowed calibration interval is -10% to +18% of leg
length. Later lifts are measured relative to that calibrated clip floor. Large
initial heights remain airborne. This is a heuristic, not authored contact metadata.

Contacts survive animation-file reloads on the same model. Seeks clear contacts
while preserving the clip-floor calibration; loop wraps reset velocity references
without inventing a new floor. Changing models resets calibration and anchors.
Long stalls clear contact history. Disabling foot planting is an immediate debug
reset; normal animated contact releases fade smoothly.

## Controls and diagnostics

Foot planting is enabled by default. Studio's Animation tab contains an A/B switch
and left/right contact/error readouts. `set_foot_lock` accepts a boolean `enabled`.
`get_state.data.foot_lock` includes the enabled flag, supported-leg count, pelvis
and clip-floor offsets, and each foot's supported/planted state, weight, target
speed/height, anchor, current position and error (model units, metres for VRM).

Gravity remains a model-authored spring-bone force for hair and clothing. The
avatar root is driven by animation; planted-foot contact is enforced by IK.

## Validation

`make test` includes a synthetic humanoid using the production transform code and
real ozz IK solver. It checks stable position/orientation, a moving pelvis during
transitions, length preservation, 30/60/144 Hz updates, lift/step/jump/turn releases,
retarget-height calibration, mid-jump resets, reach limits and unsupported rigs.
The existing transition integration test now uses production pose propagation too.
Continuity regressions reproduce an incoming knee-plane flip with an unchanged
display pose, and an 8 cm double pelvis correction during a planted transition.
They check direct/restarted/paused/reloaded blends at 30/60/144 Hz, and vanishing
pelvis movement as frame duration approaches zero.
`make test-ui` checks all local clips, standing contacts on the local reference avatar,
and release while the Jump clip is airborne. Sanitizer coverage uses the same tests.

## Remaining scope

This is a flat-floor, model-space contact layer. Uneven terrain, sloped/window
surfaces, heel/toe roll, per-model sole offsets and authored contact curves still
need explicit treatment. Root-motion extraction and locomotion/step planning are
separate work. Unusual clips starting in a low, stationary airborne pose can be
ambiguous to a height/velocity heuristic; the diagnostic switch supports review.
