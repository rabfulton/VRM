# Animation properties and automatic motion

Each animation can carry behaviour flags in its existing sibling `.cfg` file.
For example, `Enter_from_left.vrma` uses `Enter_from_left.cfg`:

```ini
loopable=0
startup=1
is_idle=0
```

A standing idle might use:

```ini
loopable=1
startup=0
is_idle=1
```

Open **⋯** beside a library entry to edit these flags. They are independent of the
LLM access checkbox. Saving preserves spring tuning, seam/bounds cache entries,
comments and unrelated settings; writes replace the file atomically. Native
**Save for this animation** preserves the behaviour flags. Legacy bounds-cache
entries are retained but no longer used for [window fitting](window-fitting.md).
Flags use exactly `0` or `1`; invalid values are disabled and reported by Studio.

## Meaning and defaults

| Property | Meaning |
| --- | --- |
| `loopable` | Whether the clip may repeat. `0` forbids repeats, including forced API loops/counts. `1` allows looping, with the existing seam treatment. |
| `startup` | Eligible as the initial entrance when Studio loads an avatar. The startup role always plays once, regardless of loopability. |
| `is_idle` | Eligible for local idle selection between automatically owned actions. A looping idle repeats; a one-shot idle advances to another idle. |

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

When Studio starts its renderer and loads an avatar, or you explicitly load a new
avatar, it chooses the first valid startup clip by library filename order, plays
it once, then transitions to an idle. If several startup clips are marked, only
the first is selected. If no startup is available, it starts an idle directly.
Attaching to a running renderer preserves its existing model and animation.

Idles form a pool selected in library order, moving to the next entry each time
an automatic return is needed. Looping idles remain until another action takes
over; they are not randomly interrupted. A one-shot idle advances when complete.
With no idle configured, Studio leaves playback alone: a one-shot holds its
final pose and a looping clip continues.

The LLM receives the loop/entrance/idle labels of its checked clips. It can only
call those clips. Separately, Studio may use any configured idle for local
fallback even if it is not exposed to the LLM. A chat one-shot finishes its clip
before returning to idle; a looped chat action can return when the reply and its
speech finish. The model does not need to issue an explicit idle cue.

Manual library playback, play/pause, seek, clip selection and playback edits cancel
automatic motion ownership. This keeps testing, scrubbing and deliberate pauses
under user control. Switching libraries and editing properties also release that
ownership. Startup is not replayed on a simple reconnect/attach to a loaded avatar.

## Live edits and runtime API

Saving properties for the currently loaded animation asks the renderer to reload
its metadata without reloading the clip, seeking or resetting physics. Changing
`loopable` to `0` finishes the current pass instead of repeating it. Changing it to
`1` permits repetition but does not force a paused/current one-shot to start looping.

`get_state.data.animation_properties` reports `loopable`, `explicit_loopable`,
`startup` and `is_idle`. `explicit_loopable` distinguishes a configured policy from
the renderer's legacy fallback. `reload_animation_metadata` rereads the current
sidecar. External file edits are reflected in Studio after library Refresh and in
the renderer after a metadata reload or animation load.

Startup/idle coordination currently belongs to Studio. The standalone renderer
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
