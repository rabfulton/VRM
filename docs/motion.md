# Motion and contact behaviour

## Implementation

The local reference avatar has six skirt chains referencing only thigh collider
groups, leaving sixteen arm/hand colliders unused for skirt motion. The runtime
augments named skirt/dress/coat/cloth chains with arm volumes, without editing
the asset or assigning those volumes to unrelated hair chains. Where an arm has
no authored collider, humanoid mappings provide conservative capsule proxies.
Studio's Physics tab exposes the policy switch, volume counts and contact residuals.

Spring simulation runs at 120 Hz with a bounded accumulator. Reference-rate damping
and forces no longer depend on render FPS. Collider motion is interpolated over
catch-up steps, collider/joint radii account for node scale, authored zero gravity
is preserved, and long stalls reset velocity. Contact projection iterates while
preserving bone length. Garments test bone segments against sphere/capsule volumes;
hair retains tail-sphere contacts with its authored collider assignments. Contact
corrections avoid injecting their push-out displacement into the next velocity.
Unsolvable anchored-root contacts remain visible in the diagnostics.

Animation transitions use the ozz sampler with a separate local-pose correction
layer in `src/transition.c`. Each transition records the blended animation-layer
pose and outgoing linear/angular/scale velocity before foot IK. Relative offsets decay with a quintic polynomial to
zero position, velocity and acceleration at completion. Quaternion log/exp and the
SO(3) Jacobian handle rotation offsets. The target animation keeps its own playback
clock; transitions advance in unpaused real update time, independently of playback
speed. Seeking cancels the transition and resets velocity history. Reload snapshots
include velocities and are validated against node identity and skeleton mapping.

Normal actions begin at their authored start. The previous hips/head-only pose
matcher could start much later in a clip and has been removed. The old weighted
fade could fall back toward rest pose for low destination weights; group settings
now multiply transition duration rather than destination influence. All groups
arrive at the full target pose. A factor of 0 switches that group immediately;
0.5 halves its duration, 1 uses the master duration, and 2 doubles it. Factors are
captured at transition start so changing a slider cannot jump a transition midway.
Default master duration is 0.45 seconds; automatic durations are bounded to 0.2–0.8
seconds. Legacy animation sampling still switches immediately.

## How to review

Restart the renderer and Studio after rebuilding. Use the ozz backend in Studio's
Animation tab. Try transitions between Relax, restless_arms, push_left and push_right.
The playback panel reports transition progress. Physics shows named garment-chain
and arm-volume counts, contacts and residual overlaps in model units displayed as
millimetres (VRM uses metres). Toggle garment/arm collisions for comparison; the
arm-overlap diagnostic measures those proxies even with the policy disabled.
The policy is session state and is not saved into animation tuning files.

`make test` runs deterministic synthetic tests for sphere/capsule shaft contact,
missing arm assignments, generated arm volumes, scaled radii, zero gravity,
30/60/144 Hz equivalence, long stalls, opposite bone directions, and impossible
anchored-root contacts. It also tests transition endpoints, rotation/translation
velocity continuity, quaternion signs, clip reload, interruption/restart,
half-speed timing, seeking, group settings and target-clock synchronization.
`make test-sanitize` instruments these tests with ASan/UBSan.

`make test-ui` exercises every local animation against the actual renderer and
samples sustained Relax/restless-arms/push poses after transitions complete. This measures skeleton proxies, not arbitrary skinned triangles. Assets are
supplied locally; see [test setup](development.md).

## Remaining work

- Garment surfaces between sparse spring chains can still intersect an arm, even
  when all bone proxies are clear. Full mesh contact needs garment panels or a
  simulation mesh mapped to the skinned surface; collider tuning alone cannot
  guarantee it.
- Animation currently owns the arms. A pose/IK contact pass is needed when the
  arm should stop or change its trajectory rather than move the clothing.
- Flat-floor foot contacts now use leg IK ([foot planting](foot-planting.md)). Root-motion extraction,
  uneven surfaces and authored contact metadata remain open.
- Fixed-step endpoint interpolation reduces missed contacts but is not continuous
  mesh collision detection. Very fast motion and incompatible collider layouts
  need dedicated swept-contact cases.
- Asset parsing/conversion still blocks the renderer during loads. Animation
  caching/preparation should move off the render thread for larger clips.
- Garment recognition currently uses conventional names. A chain inspector and
  per-model overrides are needed for models with nonstandard naming or geometry.

## Foot planting and automatic lip sync

`src/foot_lock.cpp` now observes raw target foot motion, then applies leg IK after
transition corrections. Contacts keep an ankle position/orientation anchor and
release smoothly for steps/lifts/turns. A bounded pelvis correction and clip-entry
retarget-height calibration accommodate standing clips whose feet otherwise sit
above the bind floor. See [foot planting](foot-planting.md) for precise rules, diagnostics and limitations.
Transition snapshots exclude IK corrections so they cannot be applied twice.
Knee planes follow the blended pose, leg corrections fade with contact influence,
and pelvis reach adjustments use a critically damped response. Regression fixtures
check knee flips and pelvis continuity through restarts, pauses and clip reloads.
Gravity remains in the authored spring-bone system; it does not replace contacts.

Lip-sync analysis now starts enabled. Audio-file and streamed PCM playback drive
the mouth without an enable command. Studio's switch was removed; the low-level
`lip_sync_enable` API remains for compatibility and debugging. The default test
checks that voice PCM produces visemes without configuration and silence clears them.
