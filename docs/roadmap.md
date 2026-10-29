# Path to a useful desktop companion

Priorities reviewed on 2026-10-29. This document records planned work; current
behaviour belongs in the feature guides linked below.

## Working foundation

Studio controls the standalone renderer through asynchronous IPC. Animation
playback, inertial ozz transitions, automatic blinking/lip sync, flat-floor leg
IK, fixed-step springs and garment/arm proxy collisions have regression coverage.
Local speech defaults to Nano/CrispASR on Vulkan, with saved alternative profiles,
verified downloads, preview and cancellation. QML chat now streams API replies,
stores endpoint-scoped keys in the OS keyring, speaks sentences as they arrive,
and executes user-selected library animations and supported expressions. See [chat](chat.md).

The audited CC0 starter pack and avatar import library now provide a usable first
launch without additional user assets. Native Wayland is merged and has been
exercised on a second machine. The maintained desktop targets are **Hyprland and
Xorg**. Other compositor integrations are welcome through contributed PRs; broad
compositor compatibility is not a release goal. See [Wayland support](wayland.md).

Wayland refinements will be revisited on the appropriate test machine. Explicit
monitor selection is not scheduled; revisit it if an actionable bug report or
contribution establishes a need and a way to test it. Remaining items in the
[original port plan](../wayland.md) are not all current commitments.

These features still need visual review across varied avatars. Clear skeleton
proxies do not guarantee clear clothing meshes, and flat-floor IK does not provide
desktop contact planning. See [motion limits](motion.md) and [foot planting](foot-planting.md).

## 1. Arch installation and filesystem layout

The install layout, XDG renderer paths and `domovushka-git` PKGBUILD now support
local package testing. See [packaging](packaging.md) for building from a working
tree and installing on other machines. Complete fresh-install feedback and AUR
review next. A tagged release can later support a stable `domovushka` package.

Program resources are installed read-only; starter animations get user-owned
working copies for editable sidecars. Existing Qt storage and keyring identities
are retained. Renderer preferences and sockets use user config/runtime directories,
with explicit overrides and legacy checkout settings migration.

Staged installation and distribution build flags are supported. Speech models and
optional future skills must remain optional downloads. Add **GitHub Actions builds
of Arch packages** later, reusing the checked-in PKGBUILD and checks; no hosted
package build workflow is configured yet.

Validate in a clean Arch build environment, inspect the package with namcap, and
launch from an unrelated directory with read-only installed resources and fresh
user settings. Exercise persistence, starter animation edits, upgrade preservation
and removal without deleting user data. The project is now MIT licensed and
installation includes third-party notices. The CC0 starter pack already supplies
redistributable starter/test content.

## 2. External agents through MCP

The companion's daily controls now use an avatar-coloured radial gem menu, shared
between X11 and native Wayland. Its action registry supports grouped submenus and
automatic pagination; new controls should extend that registry instead of adding
backend-specific buttons. See [menu design and extension guide](gem-menu.md).

The avatar is an MCP capability for external agents. Built-in chat is the basic
standalone option; advanced users choose External agent in Studio and use their
agent’s own interface, tools and skills. See [MCP setup](mcp.md) and the
[selected architecture](plugins.md).

The initial local stdio server exposes capabilities, queued speech/gesture
presentations, per-session status, cancellation and hide/summon through the controller’s single
renderer connection. Test agent-specific integration and the experience of using
existing filesystem, music, mail and scheduling tools to drive the avatar.

Horizontal humanoid walking now has local **Move** menu controls and MCP jobs with
duration estimates, status, cancellation and completion after arrival/idle settling.
MCP accepts relative left/right travel and absolute viewport-centre X destinations,
with geometry discovery and explicit requested/reachable positions in status.
Review gait speed and visual turns on varied humanoids, and exercise the same
controller on Hyprland. See [locomotion](locomotion.md). Extend this with authored
stride metadata, other gaits, vertical travel and pointing; target discovery
remains the external agent's responsibility. Walking and presentations currently
take turns owning the body.

Hide/summon now shares an optional exit/entrance lifecycle between Studio, the
right-click menu, MCP and an editable global shortcut. Xorg key grabs have a real
keyboard smoke check; Hyprland's Lua/Hyprlang runtime registration and config-reload
recovery still need validation on the Wayland machine. Other compositors remain
contribution-driven.

Keep ordinary speech, expressions and gestures consistent across both conversation
sources. Build future avatar-specific extensions—pointing, captions and visual
effects—on the presentation API. Add streaming and agent-input adapters as needed.
A general internal agent harness, custom executable skill ecosystem and built-in
mail/cron integrations are deferred; external agents can supply those capabilities.

## 3. Voice, conversation pacing and character behaviour

Add a small voice library with import/preview, clear per-engine capabilities and
reference-recording support where the engine provides it. Expose model-specific
controls only when supported; voice cloning, exaggeration and guidance parameters
are not universal across backends.

Chat now splits streamed replies at sentence/newline boundaries with bounded
chunks and cancellation. Add one-sentence synthesis prefetch to reduce gaps, and
reuse the coordinator for longer text entered directly in the Speech tab. Its
current preview accepts at most 4096 characters and waits for a complete WAV.

Build on the QML chat and speech/gesture coordinator with gaze, smoother emotion
ramps and richer gesture timing. Startup-to-idle handoff and automatic motion
ownership now use [per-animation flags](animation-properties.md). Review a few reliable clips, give them stable
semantic names, and curate the animation library before expanding the catalogue.
Rendering now has a 60 FPS cap and reports averaged presented FPS. Owned one-shot
motions schedule their completion checks instead of waiting for the 250 ms UI
poll. Preload/prepare the next idle before the entrance finishes to remove the
remaining synchronous clip-switch work; retain bounded memory and safe
cancellation when the avatar or library changes.
Add microphone/ASR as an optional adapter and an optional current-sentence bubble.
Broaden fixture-driven behaviour scenarios as capabilities grow.

Consider subscription-backed chat sign-in alongside API keys. See the
[ChatGPT/Grok login research note](subscription-login.md) for provider options,
implementation scope and open questions. This remains exploratory; reverify
provider support before implementation.

## 4. Better body and clothing contacts

Add collider/contact visualization and per-model chain overrides before expanding
the solver. Review arms crossing skirts, seated poses, transitions and jumps on
several rigs. Add arm pose constraints when hands should stop at contact. Explore
panel or surface-aware garment contact for gaps between sparse spring chains.
Extend foot contact to explicit surfaces and authored contact metadata.

## 5. Desktop awareness and system audio

Use separate Hyprland and Xorg adapters, starting with read-only window geometry
and a stable desktop-to-avatar coordinate mapping shared with guided help. Add
contact surfaces and sitting/standing behaviours, then opt-in window
movement with ownership and interruption rules. Window pushing needs coordinated
body motion and collision response rather than directly moving arbitrary windows.
Keep desktop skills separate from the body/contact solver: pointing to a control
or operating a media player does not require physical window pushing. Other
compositor adapters are contribution-driven extensions.

Capture system sound through a separate audio tap, extracting level/onsets for
reactions. Never consume the playback ring buffer from another `audio_read()`
consumer. Microphone input and system audio should remain separate sources.

## 6. Avatar effects: shaders, particles and trails

Explore optional visual effects around the avatar: a glitter/confetti bomb,
sparkles, smoke puffs, glow, rim lighting, coloured light pulses, motion trails
and selective motion blur. Keep this exploratory alongside the other priorities.
Use particles for individual sparks, glitter and smoke, with shaders for their
appearance and compositing; lighting and blur may need separate render passes.
Lighting affects the avatar and its effects, not unrelated desktop windows.

Start with a small sparkle/glitter burst, anchored to the avatar or a hand, with
adjustable colour, intensity and duration. Then assess smoke and glow; investigate
motion blur after establishing how temporal history behaves during animation,
dragging, resizing and visibility changes. Preview effects in Studio, allow a
global disable/intensity control, and stop rendering when an effect has expired.

Support timed animation/chat cues and future skills invoking named effect presets
with validated parameters. User-authored presets could provide new looks without
requiring changes to the conversation controller. Only implemented, enabled
effects should appear in the model's capability catalogue; cancellation and skill
disablement must invalidate pending effect triggers.

Preserve transparency and avatar-only mouse input: smoke, glitter, glow and trails
must remain click-through and stay out of the avatar hit stencil. Include their
visible extent in viewport fitting so they are not clipped, with bounded padding,
particle counts and GPU work. Review alpha blending over light/dark desktops and
measure the cost on both maintained backends before expanding the effect library.
