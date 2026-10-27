# Path to a polished desktop companion

## Working foundation

Studio controls the standalone renderer through asynchronous IPC. Animation
playback, inertial ozz transitions, automatic blinking/lip sync, flat-floor leg
IK, fixed-step springs and garment/arm proxy collisions have regression coverage.
Local speech defaults to Nano/CrispASR on Vulkan, with saved alternative profiles,
verified downloads, preview and cancellation. QML chat now streams API replies,
stores endpoint-scoped keys in the OS keyring, speaks sentences as they arrive,
and executes user-selected library animations and supported expressions. See [chat](chat.md).

These features still need visual review across varied avatars. Clear skeleton
proxies do not guarantee clear clothing meshes, and flat-floor IK does not provide
desktop contact planning. See [motion limits](motion.md) and [foot planting](foot-planting.md).

## 1. Voice and long-form speech

Add a small voice library with import/preview, clear per-engine capabilities and
reference-recording support where the engine provides it. Expose model-specific
controls only when supported; voice cloning, exaggeration and guidance parameters
are not universal across backends.

Chat now splits streamed replies at sentence/newline boundaries with bounded
chunks and cancellation. Add one-sentence synthesis prefetch to reduce gaps, and
reuse the coordinator for longer text entered directly in the Speech tab. Its
current preview accepts at most 4096 characters and waits for a complete WAV.

## 2. Behaviour and conversation in Studio

Build on the QML chat and speech/gesture coordinator with gaze, smoother emotion
ramps and richer gesture timing. Startup-to-idle handoff and automatic motion
ownership now use [per-animation flags](animation-properties.md). Review a few reliable clips, give them stable
semantic names, and curate the animation library before expanding the catalogue.
Add microphone/ASR as an optional adapter and an optional current-sentence bubble.
Broaden fixture-driven behaviour scenarios as capabilities grow.

Consider subscription-backed chat sign-in alongside API keys. See the
[ChatGPT/Grok login research note](subscription-login.md) for provider options,
implementation scope and open questions.

## 3. Better body and clothing contacts

Add collider/contact visualization and per-model chain overrides before expanding
the solver. Review arms crossing skirts, seated poses, transitions and jumps on
several rigs. Add arm pose constraints when hands should stop at contact. Explore
panel or surface-aware garment contact for gaps between sparse spring chains.
Extend foot contact to explicit surfaces and authored contact metadata.

## 4. Desktop awareness and system audio

Start with read-only X11 window geometry and a stable desktop-to-avatar coordinate
mapping. Add contact surfaces and sitting/standing behaviours, then opt-in window
movement with ownership and interruption rules. Window pushing needs coordinated
body motion and collision response rather than directly moving arbitrary windows.

Capture system sound through a separate audio tap, extracting level/onsets for
reactions. Never consume the playback ring buffer from another `audio_read()`
consumer. Microphone input and system audio should remain separate sources.

## 5. Distribution and release quality

Package the renderer, Studio and dependency notices; offer optional speech/model
downloads with visible progress and recovery. Validate a clean clone, empty user
configuration and GPU/driver failures. Add a redistributable test asset only with
explicit permission, choose a project license, and broaden platform support after
the Linux/X11 release works reliably.
