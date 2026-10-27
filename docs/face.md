# Facial expression balance during speech

Emotions can combine with visemes and still overwhelm them visually: a strong
smile or surprised mouth may dominate subtler audio-driven shapes. Chat now
requests a moderate emotion weight of **0.55**, rather than 0.8.

The renderer also reduces standard emotion presets to **40% of their requested
strength** during speech. A chat emotion therefore settles near **0.22** while
talking. The transition eases in with a 60 ms time constant and restores the
requested strength with a 150 ms time constant after playback. This affects
manual Studio expressions as well as chat. Vowel and blink inputs are not reduced,
and the audio analyser's gain/threshold remain unchanged.

Utterance activity uses an open PCM stream or buffered audio, with lip sync
enabled. It does not follow instantaneous volume, so short pauses and packet gaps
do not pump the emotion strength. Cancellation/drain releases the speech balance.
Chat's independent four-second emotion lifetime may expire before playback ends;
an expired emotion remains cleared rather than being restored.

`src/expressions.c` keeps each expression's requested weight independently and
composes managed morph targets additively, bounded to [0, 1]. Clearing one input
cannot erase another input using the same target. Unmanaged authored targets are
left alone. The balancing pass runs in the renderer, outside network/GUI timing.
Legacy/new vowel and emotion aliases refer to the same named inputs.

The policy recognizes happy/joy, angry, sad/sorrow, relaxed/fun and surprised.
Unknown custom expressions are not automatically classified. This is whole
expression attenuation: a single full-face morph cannot reliably be separated
into mouth and brows without model-specific information. Separate upper/lower
face controls or per-model masks are the next refinement if stronger brows/eyes
are wanted while preserving articulate speech. Binary expressions stay binary;
they may switch off while talking because they cannot fade continuously.

`get_state.data.face` exposes `speech_active` and `emotion_scale` for inspection.
The latter is a multiplier, not the emotion's requested weight. VRM 1.0 procedural
override metadata is not implemented by this balancing policy.

`make test` and `make test-sanitize` exercise real PCM-derived visemes with emotion
attenuation, smooth recovery, 30/60/144 Hz equivalence, shared-target composition,
aliases, invalid bindings and binary output. `make test-chat` checks real
Nano/Vulkan playback and the live renderer's attenuation/recovery diagnostics.
