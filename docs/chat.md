# Chat and character actions

Studio includes a compact, separate Qt Quick chat window. It shares the existing
renderer connection and speech profile; it is not another IPC client.

## Start a conversation

1. Install `requirements-gui.txt`, build with `make`, then start `python3 control.py`.
2. Open **Chat…** in Studio, press **Ctrl+Enter** while this application has focus,
   or right-click the avatar. Right-click opens the chat near the pointer, kept
   within the current screen. The shortcut is application-wide, not an OS global hotkey.
3. In chat **Settings**, enter the full OpenAI-compatible Chat Completions URL
   and the model ID supplied by your provider. The default URL is
   `https://api.openai.com/v1/chat/completions`; no model is chosen automatically.
4. Paste your API key and Save. Prepare a voice in Studio's Speech tab if needed.
5. Type a question and press Enter. Shift+Enter adds a line. Escape hides chat.

A new question interrupts the current reply. **Stop** cancels the API request,
queued utterances, owned TTS inference and pending gestures. Already received
text stays visible. **New** clears conversation context. The transcript and
rolling API history stay in memory, with no chat-history file.

Chat works as text without a connected avatar. Speech and avatar actions require
a loaded model. Voice/profile changes, model/library changes and renderer
disconnects stop the active character turn. A speech failure leaves the text
response available and suppresses further speech for that turn.

## API keys

The `keyring` Python package talks to the operating system credential service:
Secret Service/KWallet on Linux, Keychain on macOS, or Credential Manager on
Windows. Linux needs a running, unlocked desktop credential service. The app does
not substitute a plaintext or file backend. An unavailable/locked store produces
an error so you can unlock it and retry. Keyring calls run outside the GUI thread.

Keys use service `vrm-overlay.chat` and a SHA-256 account identifier derived from
the normalized endpoint URL. Changing providers does not send the old provider's
key to the new endpoint. The password field is cleared when the settings dialog
closes; **Check saved key** reports presence without displaying it, and **Remove
saved key** removes that endpoint's credential. Settings contain endpoint/model,
character text and preferences, never the key. API errors do not display raw
server bodies that might echo credentials. Redirects are not followed.

Remote endpoints require HTTPS. **No API key (local server only)** supports local
OpenAI-compatible servers on `localhost`, `127.0.0.1` or `::1` without keyring access.
Use the full `/v1/chat/completions` URL, not only a provider's base URL. APIs using
other authentication or response formats need a separate adapter.

The old Tkinter example also uses this credential service. On load, a legacy
plaintext key is migrated and removed from its settings only after confirmed OS
storage. If storage fails, the old file is left intact and migration reports an
error. New saves omit the key from JSON.

## Character prompt and cues

Settings contain an editable character description and a preview of the current
system prompt. The app appends speech formatting and a capability catalogue from
the loaded avatar/library. In Studio's **Animation Library**, check the box next
to each animation the LLM may use. Clicking its name still plays it manually;
checking it changes access without starting playback. The prompt preview updates
immediately, and the next API request includes only the checked animations.

Selections are remembered per library folder, including across restarts. Newly
added files start unchecked. Changing access stops an active chat turn and
invalidates pending speech/gestures from its old catalogue. An animation already
playing is left in its current playback state. The global **Allow gestures and
expressions** setting can disable all character actions without losing selections.

On the first scan of a library, the original four gestures keep their defaults:

| Cue | Local animation stem | Behaviour |
| --- | --- | --- |
| `[[animation:idle]]` | `Relax` | Looped relaxed pose |
| `[[animation:thinking]]` | `Thinking` | Looped thinking pose |
| `[[animation:greet]]` | `VRMA_02` | One-shot greeting |
| `[[animation:angry]]` | `Angry` | One-shot annoyed gesture |

Any other VRMA/GLB animation can be enabled through its checkbox. Cue names are
generated from filenames (for example `Clapping.vrma` becomes
`[[animation:clapping]]`). A short suffix distinguishes colliding names; hover the
checkbox to see the exact cue. Playback honours each animation's `loopable` flag;
startup/idle labels are also included. **⋯** in the library edits these
[animation properties](animation-properties.md). Names stay the same when boxes are toggled
or the list is filtered. The model receives a label and cue, not a local path.
Missing and unchecked files are omitted, and unlisted cues cannot execute.

Choices live in QSettings under `chat/animation_libraries`, keyed by a digest of
the library folder and then by filename. Moving/renaming a file makes it a new
choice. No animations are downloaded, renamed or removed by this feature.

Expressions are mapped from the avatar's actual names to `happy`, `angry`, `sad`,
`surprised` and `neutral`, where supported. Cues request a moderate 0.55 weight;
while speech plays, the renderer smoothly reduces emotion influence to 40% of
the request (about 0.22 for chat) and restores it after playback. The chat
expression releases after four seconds or Stop. Lip-sync and blink expressions
are not exposed as chat emotion controls. The initial implementation resets only
the expression owned by chat, rather than clearing every morph. See
[facial expression balance](face.md) for the renderer policy and limitations.

For Nano/Turbo, the prompt permits the nine sound tags used by Chatterbox Turbo:
`[gasp]`, `[laugh]`, `[chuckle]`, `[sigh]`, `[cough]`, `[groan]`, `[sniff]`, `[shush]`
and `[clear throat]`. Tags remain in speech sent to the native engine. Other
providers omit these directions. The parser removes unsupported tags from speech.
Actual vocal delivery depends on the model/voice; the app does not synthesize
sound effects independently.

Example model output:

```text
[[expression:surprised]][gasp] Oh! You surprised me.
[[animation:thinking]]Let me think about that.
[[animation:idle]][[expression:happy]]Here is an idea.
```

Only complete, allowlisted action cues execute. Cues split across network chunks
are buffered; unknown, incomplete and excessive cues cannot become IPC commands.
They are excluded from spoken text. Code spans/fences are displayed without
speech or actions. Gestures are limited to eight per reply and spaced at least
750 ms apart. They run in the same ordered queue as sentence playback: a cue
before a sentence takes effect before synthesis/playback of that sentence.
This is sentence-level coordination, not word-level gesture timing. Configured
idles provide a local fallback after chat actions; one-shots finish their clip
before returning, and manual playback/seek/pause cancels automatic ownership.

## Streaming and limits

`qt_chat_network.py` uses Qt Network with incremental UTF-8/SSE decoding; it adds
no HTTP SDK dependency. Complete sentences are sent to the existing local TTS
provider as text arrives. Sentence/newline boundaries and a 360-character fallback
bound synthesis work. The first sentence can play while the API is still
streaming. Each sentence currently synthesizes a complete WAV and drains through
the normal PCM path before the next one; audio-token streaming and synthesis
prefetch are future improvements.

Inputs are capped at 4,000 characters and replies at 16,000. Pending speech/actions
are bounded by that reply limit. API context retains at most 24 completed messages
and 32,000 text characters, dropping old turn pairs. Incomplete/error replies are
visible but excluded from subsequent API context. The UI retains 40 messages.
The API has a 60-second transfer timeout and a three-minute overall deadline.
There is no automatic retry that could duplicate a request or gesture.

## Desktop actions and the chat UI direction

Window pushing/shaking, sitting on windows, desktop inspection and system-audio
reactions are explicitly unavailable in the prompt. The model must not claim it
can perform them. The same capability catalogue can advertise them once an
executor exists; adding prose to the prompt alone does not create a capability.

The next desktop milestone is read-only X11 geometry, explicit target selection,
and desktop-to-avatar contact coordinates. A bounded shake of a selected window
can follow, restoring its original position and stopping when the user moves it.
Physical pushing/sitting then needs body contact/IK, motion ownership and release
rules. Attention requests should be rate-limited and optional. Keep those rules
in the executor rather than depending on the model to remember them.

Keep the compact window as the full transcript/composer/settings surface. A small
caption bubble near the avatar can later mirror the spoken sentence. This avoids
anchoring a long transcript to a moving figure while leaving room for a more
characterful presentation.

## Validation and protocol references

`make test` covers fragmented SSE/UTF-8, HTTP failures/redirects, keyring storage
and migration, partial cues, code spans, bounded output, early speech, interruption,
stale profile results and actual QML controls with PCM transport. Tests use local
HTTP fixtures and fake credentials/voices, never a paid API request.

`make test-chat` additionally uses the real renderer, local avatar/animation assets
and Nano on Vulkan with a local streamed LLM fixture. It exercises QML send,
expressions, thinking/idle gestures and playback drain. See [test setup](development.md).

Protocol sources: [OpenAI streaming reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/streaming-events),
[keyring documentation](https://keyring.readthedocs.io/en/stable/),
[Chatterbox Turbo sound tags](https://github.com/resemble-ai/chatterbox/blob/master/gradio_tts_turbo_app.py),
[Nano backend compatibility](https://huggingface.co/cstr/chatterbox-nano-GGUF).
