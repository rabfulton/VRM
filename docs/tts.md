# Local speech

Studio defaults to **Chatterbox Nano through CrispASR on Vulkan**. CPU is an
explicit alternative. The Speech tab selects
Nano, Turbo or Kokoro, CPU/Vulkan processing, or the alternative Piper provider.
Advanced settings accept other TTS backend IDs supported by the installed CrispASR
build. No Python ML packages are installed by this integration.

## First use

Start `python3 control.py`, open **Speech**, and click **Prepare voice**. It
downloads a missing native engine and caches the selected model. Nano/Turbo model
files are pinned to repository revisions and verified with SHA-256. Interrupted
downloads resume on the next Prepare attempt; the two models share one decoder.
Other backends use CrispASR's native model resolution.
Nano's model pair is about 700 MB; Turbo's about 1 GB. Preparation also works
before connecting to the renderer. Then use **Try the voice** -> **Speak**.

Preparation/synthesis run off the GUI thread. Stop cancels them and queued
playback. Profile switches invalidate late audio from the old synthesis.
Each provider/model remembers its own paths, voice, language, device and threads;
Nano -> Turbo -> Nano restores Nano's profile. Studio persists these in QSettings
under `speech/profiles`, independently of animation tuning.

The installer pins CrispASR **v0.8.45**, checks the publisher's SHA-256 digest,
and preserves native libraries, runtime assets and license notices together.
CPU x86 downloads use the legacy build to avoid requiring AVX2. Native packages
are selected for Linux, Windows and macOS, including Linux ARM/Apple Silicon.
Vulkan requires a working driver and is not assumed to fall back to CPU.
On macOS, select automatic processing for the native Metal package, or CPU,
rather than the Linux/Windows Vulkan artifacts.
Unsupported combinations allow a user-supplied executable.

## Files and terminal setup

Default storage roots:

- Linux: `${XDG_DATA_HOME:-~/.local/share}/vrm-overlay/speech`
- macOS: `~/Library/Application Support/vrm-overlay/speech`
- Windows: `%LOCALAPPDATA%/vrm-overlay/speech`

`VRM_SPEECH_HOME` overrides the root. Models use its `models/` directory unless
`CRISPASR_CACHE_DIR` is set. A prepared `build/speech/models` is also reused when
there is no user cache or environment override. Runtime lookup checks the configured executable,
installed package, this checkout's `build/speech`, and `crispasr` on PATH.
Explicit model/decoder paths support offline use once all companion resources
are present. Blank paths request native automatic resolution.

```sh
python3 scripts/setup_speech.py --prepare
# Explicit CPU alternative:
python3 scripts/setup_speech.py --device cpu --prepare
# Keep runtime and models in the checkout during development:
python3 scripts/setup_speech.py --directory build/speech --prepare
```

Piper is not installed automatically: select its executable, local `.onnx` voice
and matching `.onnx.json`. The chat/TTS example uses the same provider factory and
now defaults to Nano. Old settings with an explicit Piper voice migrate to Piper.

## Runtime boundary

`vrm_overlay/tts.py` defines validated configuration and `create_tts()`. Providers
implement `synth_wav(text, path, timeout_s, stop_event)` and optionally
`prepare(stop_event)` / `close()`. Add a provider to this factory/catalogue without
changing renderer IPC, lip sync or audio timing.

CrispASR runs as an owned, authenticated loopback sidecar, loading the selected
model once and reusing it through `/v1/audio/speech`. A worker converts WAV output
to 24 kHz mono signed 16-bit PCM. Studio uses its existing asynchronous IPC
connection, buffers roughly half a second, and waits for drain before Ready.
Studio chat coordinates a bounded queue of speech and character cues over the
same transport. The older chat example retains a one-second audio window.

Cancelling native inference terminates our sidecar because an HTTP disconnect
alone does not guarantee computation stops. The next utterance reloads it. Idle
models remain resident until a profile change or exit. Studio preview receives a
complete WAV before playback; it does not claim token-level audio streaming.
Studio chat synthesizes sentence by sentence as API text streams, with cues and
cancellation managed by the [chat controller](chat.md). Voice preview retains its
4096-character limit. True audio-token streaming, sentence prefetch and a managed
voice library remain planned work in the [roadmap](roadmap.md).

The default uses the baked voice. Custom voice packs are passed to the engine;
reference-recording cloning has additional engine-specific requirements. Runtime
and model licenses remain separate; keep downloaded component notices.

## Checks

`make test` checks persistent reuse, real fixture HTTP, cancellation/restart,
errors, paths with spaces, CPU/Vulkan arguments, profile persistence, stale-result
rejection, QML playback/drain and installer verification without voice downloads.
Model-download tests check range resumption, interrupted transfers, ignored ranges
and checksum failures without replacing an existing model.
`make test-ui` exercises the renderer and all animations without TTS downloads.
`make test-speech` also synthesizes real Nano on Vulkan and streams it into the renderer.
Prepare the voice first, or allow its initial download. For a Vulkan check:

```sh
python3 tests/qt_smoke.py --speech --speech-device vulkan
# Equivalent make target:
make test-speech SPEECH_DEVICE=vulkan
```

Upstream: [pinned release](https://github.com/CrispStrobe/CrispASR/releases/tag/v0.8.45),
[server API](https://github.com/CrispStrobe/CrispASR/blob/main/docs/server.md),
[Nano](https://huggingface.co/cstr/chatterbox-nano-GGUF),
[Turbo](https://huggingface.co/cstr/chatterbox-turbo-GGUF).
