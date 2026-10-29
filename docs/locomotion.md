# Relative and absolute horizontal walking

Right-click the avatar, open **Move**, then choose **Walk left** or **Walk right**.
Each request travels up to 240 screen units. **Stop walking** interrupts it and
returns to standing idle. Hide remains in this submenu too.

This first implementation supports visible, standing humanoid avatars. It uses
the bundled CC0 in-place walking cycle, turns the model toward travel, eases its
speed up/down, turns back and blends into an enabled standing idle. Without a
configured standing idle it uses the bundled idle. The user's playback speed is
restored. The fox and other non-humanoid rigs do not yet have a locomotion role.
Wait for startup or a pose transition to finish before walking.

For locomotion the loader removes the clip's baked side-facing yaw before
retargeting. The movement controller alone turns the avatar left/right by the
shortest path and returns it to its original yaw. Normal library previews keep
their authored facing; source animations and their sidecars are unchanged.

## MCP jobs and completion

Choose **External agent** in Studio to enable walking from MCP. `avatar_walk`
accepts either a relative walk:

```json
{"direction":"left","distance":240}
```

or an absolute horizontal destination:

```json
{"x":900}
```

For relative walks, distance is optional (default 240), positive and bounded to
24–1600. Absolute walks accept a finite `x` from -131072 to 131072, including
negative desktop coordinates and fractional values. Do not combine `x` with
`direction` or `distance`. Absolute walks can travel farther than 1600 and can
make small positioning moves or request the current position.

Units follow the renderer's screen geometry: X11 desktop pixels or native Wayland
output logical coordinates. Absolute X addresses the **viewport/camera centre**,
not its left edge. It is resolved after gait/turn viewport fitting. Surface
placement rounds to pixels; arrival uses a two-unit tolerance. Y is unchanged.
Both forms stay within the current workarea; no monitor selection or migration
is added. On Wayland coordinates are local to the selected output.

`avatar_capabilities.walking` advertises `modes`, `anchor`, current `x`, `workarea`,
relative-distance limits and absolute-coordinate limits. Query it before choosing
a destination and after local movement or desktop changes.

The result immediately includes an `id`, `state`, `estimated_duration_seconds`,
`remaining_seconds`, `progress`, `requested_distance`, `distance`,
`actual_distance` and `clamped`, plus `mode`, `start_x`, `requested_x`, `target_x`
and current `x`. `target_x` is refined to the reachable centre coordinate after
fitting/clamping. Absolute jobs report the resulting `direction` (`left`, `right`
or `none`). The initial estimate uses the pinned gait and
current avatar height. Native preparation refines it after viewport fitting and
screen-edge clamping. Asset loading, scheduling and rendering stalls can change
the estimate. Remaining time is an estimate too, not an authoritative deadline.

Poll `avatar_motion_status` with `{"motion_id":"<returned id>"}`. Phases are
`preparing`, `turning`, `walking`, `turning_back`, optionally `arriving`, then
`settling`. **Only `completed`, `cancelled` and `failed` are terminal.** Completion
requires both actual arrival (within two screen units) and a fresh renderer
snapshot confirming that the return-to-idle blend has ended. `progress` tracks
native travel/turning, so it can reach 1 while the final idle blend is settling.
There is no unsolicited completion callback or agent wake-up in this version.

`requested_distance` and `distance` are magnitudes; `actual_distance` is signed
(negative left, positive right). The distance is reduced at the current workarea's
edge and `clamped` reports that reduction. A request with no available space still
settles successfully with zero travel. No output selection or migration is added.

Use `avatar_motion_cancel` with the same motion ID to stop and return to idle.
Poll status until terminal. Jobs belong to their adapter session; another session
cannot inspect or cancel them. Disconnecting the adapter cancels its walk. Up to
128 records are retained in memory; IDs do not survive restarts. Only one walk can
own the body at a time; other requests are rejected rather than silently queued.
Request speech/gestures after walking finishes or is cancelled.

Manual playback, avatar replacement, hiding and conversation-source changes
interrupt walking. Dragging the avatar, orbiting or zooming interrupts native
travel as well. The stopped screen location is retained. Late queued start/play
commands are invalidated before stopping, so an interrupted walk cannot restart.

## Rendering and gait

`src/walk_motion.c` owns a monotonic-time travel curve with cosine acceleration
ramps and smooth yaw turns. `src/main.c` applies it before animation sampling.
Gait time follows actual travel distance; nominal stride is currently 45% of the
projected clip height per cycle, so some foot sliding remains across avatars.
The audited gait disables flat-floor foot planting, preserving the global user
preference and restoring normal per-clip policy on idle. It does not pretend that
stationary floor IK can anchor feet while the viewport moves.

The renderer fits a local envelope covering the gait and yaw turn once, then
freezes viewport fitting while travel is active. X11 moves the fixed-size window;
native Wayland moves the avatar viewport within its existing output canvas.
There is no viewport covering the whole journey and no per-frame resize. Normal
idle fitting resumes afterwards. External desktop layout changes are not routed
or replanned during a move.

Jogging, crawling, calibrated stride metadata, world-aware contacts, vertical
travel and pointing are follow-up work. This horizontal API provides a native
motion controller and explicit job lifecycle to extend without embedding an
agent harness in the application.

## Verification

`make test` covers travel curves, endpoint stability, cancellation, ownership,
invalid tool inputs, clamping status, failed loads and idle-blend completion.
`make test-walk` uses the real renderer and private companion API, with bundled
assets and isolated settings; no voice or model API is required. It checks both
directions, relative and absolute destinations, small/already-reached targets,
fixed travel viewport sizes, clamping, speed restoration and stop.
Run `python3 tests/walking_smoke.py --backend wayland` on the Hyprland machine.
