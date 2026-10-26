# Chat and local speech example

A separate Tkinter integration prototype: stream a Chat Completions reply from an
OpenAI-compatible endpoint, split it into sentences, synthesize local speech and
send bounded 24 kHz mono PCM to the renderer with automatic lip sync.

## Setup

Run these commands from the repository root:

```sh
make
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r examples/chat_tts/requirements.txt
python3 scripts/setup_speech.py --prepare
./vrm-overlay /path/to/avatar.vrm
```

In another terminal with the same environment:

```sh
export OPENAI_API_KEY="your-key"
python3 examples/chat_tts/chat_tts_overlay.py
```

Set the endpoint/model, avatar and animation paths in Settings. The old local
defaults `test.vrm` and `animation/Relax.vrma` are not bundled. Disconnect Studio
before connecting this example: the runtime supports one active controller.

## Speech and settings

Nano through CrispASR on Vulkan is the default. Settings select the provider,
CrispASR backend (Nano, Turbo, Kokoro or another supported ID), device and optional
model/decoder/voice paths. Blank CrispASR paths request automatic resolution.
See [local speech](../../docs/tts.md) for downloads, cache locations and limits.

Piper is optional and requires its executable, a local `.onnx` voice and matching
`.onnx.json`. Existing settings with an explicit Piper voice retain that provider;
legacy `PIPER_BIN` and `PIPER_MODEL` environment variables are also supported.
Studio's provider profiles are stored separately from this example's settings.

Settings and logs live under `${XDG_CONFIG_HOME:-~/.config}/vrm-overlay/` as
`chat_tts_settings.json` and `chat_tts.log`. Theme and auto-blink are configurable.

Stop cancels queued playback and inference; late sentences are discarded.
CrispASR stays resident between sentences and is terminated on cancellation.
Queues are bounded and playback buffers roughly one second. `Idle` means the
runtime playback queue drained; device latency can add a small delay.

The shared client matches every acknowledgement by request ID. See the
[IPC reference](../../docs/api.md) and [architecture](../../docs/architecture.md).
Headless transport/audio/cancellation checks run with `make test`, without an AI
service or voice download. Conversation controls are not yet part of Studio.
