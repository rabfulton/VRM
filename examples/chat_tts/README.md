# Chat + Piper TTS example (vrm-overlay)

This example shows how to control `vrm-overlay` via the IPC API (`API.md`) while:

- Streaming a Chat Completions response from an OpenAI-compatible endpoint
- Splitting the response into sentences
- Running Piper TTS per sentence
- Streaming 24kHz 16-bit mono PCM to `vrm-overlay` (`audio_stream_*`), enabling lip sync

## Prereqs

- Build/run the overlay: `make && ./vrm-overlay /path/to/model.vrm`
- Python 3.10+
- Piper CLI + a voice model (`piper --help`)

Example env:
```bash
export OPENAI_API_KEY="..."
export PIPER_BIN="piper"   # or piper-tts, depending on your install
export PIPER_MODEL="/path/to/voice.onnx"
```

Piper voices typically require both files side-by-side:
- `voice.onnx`
- `voice.onnx.json`

## Install

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r examples/chat_tts/requirements.txt
```

## Run

```bash
source .venv/bin/activate
python examples/chat_tts/chat_tts_overlay.py
```

## Notes

- The overlay IPC socket defaults to `/tmp/vrm-overlay.sock`.
- Audio is streamed as base64-encoded PCM chunks at 24kHz mono s16le (`audio_stream_chunk`).
- Settings are saved to `~/.config/vrm-overlay/chat_tts_settings.json` by default (or `$XDG_CONFIG_HOME/vrm-overlay/chat_tts_settings.json` if set).
- You can also set `OPENAI_API_KEY`, `PIPER_BIN` (default `piper`), and `PIPER_MODEL` (voice `.onnx`) via env vars.
- Default model/animation are set to this repo’s `test.vrm` and `animation/Relax.vrma` (override in Settings).
- The default animation is forced to loop via `blend_to(..., loop=true)`.
- Theme defaults to `auto` (tries to follow your OS dark/light preference); you can override to `light`/`dark` in Settings.
- Auto-blink is enabled by default (toggle in Settings).
- Debug log is written to `~/.config/vrm-overlay/chat_tts.log` (or `$XDG_CONFIG_HOME/vrm-overlay/chat_tts.log`).
