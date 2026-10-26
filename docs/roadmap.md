# Path to a polished desktop companion

## Working foundation

Studio controls the standalone renderer through asynchronous IPC. Animation
playback, inertial ozz transitions, automatic blinking/lip sync, flat-floor leg
IK, fixed-step springs and garment/arm proxy collisions have regression coverage.
Local speech defaults to Nano/CrispASR on Vulkan, with saved alternative profiles,
verified downloads, preview and cancellation.

These features still need visual review across varied avatars. Clear skeleton
proxies do not guarantee clear clothing meshes, and flat-floor IK does not provide
desktop contact planning. See [motion limits](motion.md) and [foot planting](foot-planting.md).

## 1. Voice and long-form speech

Add a small voice library with import/preview, clear per-engine capabilities and
reference-recording support where the engine provides it. Expose model-specific
controls only when supported; voice cloning, exaggeration and guidance parameters
are not universal across backends.

Reuse the chat example's bounded sentence pipeline in Studio: split at sentence,
newline and paragraph boundaries, start playback before an entire long passage
is synthesized, and maintain cancellation/backpressure. The current Studio
preview accepts at most 4096 characters and waits for a complete WAV. Test long
input, abbreviations, multilingual punctuation and interruption between chunks.

## 2. Behaviour and conversation in Studio

Move the useful chat prototype into QML and introduce a behaviour coordinator
for speech, gaze, expression, gestures and idle motion. Keep typed input first;
add microphone/ASR as an optional adapter. Agent output should request bounded,
validated actions through the same runtime API. Establish reproducible behaviour
scenarios so every control and cancellation path can be reviewed without an AI
service.

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
