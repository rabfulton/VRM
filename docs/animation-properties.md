# Animation properties and automatic motion

Each animation can carry behaviour flags in its existing sibling `.cfg` file.
For example, `Enter_from_left.vrma` uses `Enter_from_left.cfg`:

```ini
loopable=0
startup=1
is_idle=0
llm_enabled=0
```

A standing idle might use:

```ini
loopable=1
startup=0
is_idle=1
llm_enabled=0
```

Open **⋯** beside a library entry to edit these flags, including **Available to LLM**.
The sidebar checkbox enables automatic use. An enabled idle can run locally while
**Available to LLM** is off; an unchecked entry is excluded from startup, idles,
automatic transitions and LLM capabilities. Clicking any name explicitly previews
one pass, including unchecked and loopable clips. Previous/Next also preview every
entry. Previewing changes neither checkbox nor LLM access, and holds the final
pose until another manual action or **Resume idle**. Pause, seek and playback
controls remain available for that preview.
Saving preserves spring tuning, seam/bounds cache entries,
comments and unrelated settings; writes replace the file atomically. Native
**Save for this animation** preserves the behaviour flags. Legacy bounds-cache
entries are retained but no longer used for [window fitting](window-fitting.md).
Flags use exactly `0` or `1`; invalid values are disabled and reported by Studio.

## Meaning and defaults

| Property | Meaning |
| --- | --- |
| `loopable` | Whether the clip may repeat. `0` forbids repeats, including forced API loops/counts. `1` allows looping, with the existing seam treatment. |
| `startup` | Eligible as the initial entrance when Studio loads an avatar. The startup role always plays once, regardless of loopability. |
| `is_idle` | Eligible for the automatic idle chain between character actions. Each idle gets a pass before the next is selected. |
| `is_exit` | **Play before hiding**. Defaults to `0`; enabled exits play once before hiding and are excluded from the idle pool. Independent of `exit_pose`, which describes body posture. |
| `llm_enabled` | **Available to LLM**. Offer the clip to the chat model only while its sidebar master switch is also on. Stored in the sidecar; absent values inherit the existing LLM choice or conservative first-use defaults. |
| `entry_pose`, `exit_pose` | `standing`, `sitting`, `lying` or `crouching`; default `standing`. Describe pose compatibility for automatic actions. |
| `floor_contact` | `0` releases flat-floor IK for this clip; `1` permits it when the user's global foot locking is enabled. Defaults to `1`. |

For old files without flags, Studio recognizes `Enter_from_left` as a one-shot
startup and `Relax` as a looping idle. `Thinking` remains loopable but is not an
idle by default. Other unclassified clips default to one-shot/non-startup/non-idle
in Studio. Explicit flags override these defaults, including `startup=0`.
The renderer retains its older geometric loop heuristic when no explicit
`loopable` property exists and the API does not specify a loop choice.

These defaults do not write to every animation automatically. The local reference
`Enter_from_left.cfg` and `Relax.cfg` are explicitly marked for entrance and idle.
They remain user-owned ignored assets, not redistributed test fixtures.

## Startup and idle selection

**Play at startup** assigns a role; the animation's sidebar checkbox must also
be on for automatic use. **Available to LLM** can stay off: startup playback does
not require giving the AI access to that clip.

The entrance is also used when summoning a hidden avatar. See Studio's
[hide/summon lifecycle](studio.md#hiding-and-summoning) for exit selection,
interruptions and shortcuts.

When Studio starts its renderer and loads an avatar, or you explicitly load a new
avatar, it chooses the first enabled, valid startup clip by library filename order, plays
it once, then transitions to an idle. If several startup clips are marked, only
the first is selected. If no startup is available, it starts an idle directly.
With a startup configured, the model stays invisible and non-interactive during
loading and clip setup. Its first presentation uses the startup pose directly,
without blending from the model's bind pose. Playback reveals it; a failed load
keeps it hidden until a valid animation is played (or the model is loaded again
without deferred presentation). Standalone model loads retain immediate display.
Attaching to a running renderer preserves its existing model and animation.

Idles form a pool selected in library order, skipping an immediate repeat when
another idle is available. With several idles, every clip plays once, including
clips marked loopable, then blends into the next idle when it completes. A lone
loopable idle loops without repeatedly loading the same file; a lone one-shot
idle is replayed through a transition. Startup always plays once before this chain.
Idle changes wait while chat or speech is active, and character gestures take
priority. The pool uses enabled, valid `is_idle` clips regardless of LLM access.
Selection first uses stable idles in the current pose, so lying/sitting characters
do not jump into a standing idle. A lone idle in that pose stays resident.
With no idle configured, Studio leaves playback alone: a one-shot holds its
final pose and a looping clip continues.

The LLM receives the loop/entrance/idle labels of clips with both master enable
and **Available to LLM** switched on. It can only call those clips. Separately,
Studio may use an enabled idle for local fallback even if it is not exposed to
the LLM. A chat one-shot finishes its clip
before returning to idle; a looped chat action can return when the reply and its
speech finish. The model does not need to issue an explicit idle cue.

Manual library playback, play/pause, seek, clip selection and playback edits cancel
automatic motion ownership. This keeps testing, scrubbing and deliberate pauses
under user control. Switching libraries and editing properties also release that
ownership. Startup is not replayed on a simple reconnect/attach to a loaded avatar.
Use **Resume idle** in Studio to hand control back to the idle chain after testing.

Disabling the current automatic animation pauses it and cancels queued automatic
playback for that clip. An explicit manual preview can continue regardless of
the checkbox. Disabling the current automatic idle hands off to another enabled idle
when a compatible one exists; otherwise it stays paused. Disabling a future
transition also cancels the pending route. Re-enabling does not revive old queued
commands. Master choices live in QSettings under `animations/enabled_libraries`,
per folder and filename. Existing sidebar check states migrate to this master
map once. Fresh libraries/new files start master-enabled; newly discovered clips
remain unavailable to the LLM unless explicitly configured otherwise.

## Pose transitions and floor contact

**Starts in** and **Ends in** describe the poses already authored into the clip.
They neither trim the timeline nor modify the motion to match those labels.

| Animation | Starts in | Ends in |
| --- | --- | --- |
| Standing idle / walk | standing | standing |
| Sit down | standing | sitting |
| Seated idle | sitting | sitting |
| Stand up | sitting | standing |
| Get up from the floor | lying | standing |

Automatic actions find a shortest route through enabled, valid one-shot clips
with different entry/exit poses. For example, a walk requested from a lying idle
runs `get_up` before starting the walk. Internal transitions may have LLM access off;
they are local coordination like startup/idle fallback, not advertised agent cues.
A transition requested while already in its destination pose is a no-op. No route,
invalid metadata or an unfinished pose change refuses the automatic cue. Cancellation,
manual pause and metadata edits invalidate queued route steps.

Manual library playback stays direct for inspecting clips. **Resume idle** uses
the current pose and any available return route. The [starter pack](../assets/starter/README.md)
includes seated/lying idles and their transitions. Its reverse-derived lie-down
is a preview; walking/climbing are in place and do not move desktop windows.

`floor_contact=0` is useful for climbing, getting up, lying, sitting and authored
locomotion. It never changes the global foot-lock preference. Changing the policy
releases previous anchors; future allowed clips can plant normally.

## Live edits and runtime API

Saving properties for the currently loaded animation asks the renderer to reload
its metadata without reloading the clip, seeking or resetting physics. Changing
`loopable` to `0` finishes the current pass instead of repeating it. Changing it to
`1` permits repetition but does not force a paused/current one-shot to start looping.

`get_state.data.animation_properties` reports `loopable`, `explicit_loopable`,
`startup`, `is_idle` and `floor_contact`. `llm_enabled` and the sidebar master gate
belong to Studio, rather than this native state object. `foot_lock.clip_allowed` exposes the
per-clip policy independently of `foot_lock.enabled`. `explicit_loopable` distinguishes a configured policy from
the renderer's legacy fallback. `reload_animation_metadata` rereads the current
sidecar. External file edits are reflected in Studio after library Refresh and in
the renderer after a metadata reload or animation load.

Startup/idle coordination belongs to Studio's background controller and continues
when its windows are hidden. The standalone renderer
has no animation-library directory to select from; independent IPC clients must
coordinate those behaviours themselves. See the [IPC reference](api.md).

## Verification

Headless tests cover strict flags, atomic save failure, preservation of tuning and
future settings, duplicate keys, model-load startup, one-shot idle cycling,
manual-pause ownership, stale state snapshots and chat-to-idle handoffs.
Sanitizers cover the native config writer and loop-policy enforcement.
`make test-chat` additionally checks the real entrance-to-idle transition, forced
loop/count rejection, live metadata reload without seeking, all local animations
and Nano/Vulkan speech with a local LLM fixture.
