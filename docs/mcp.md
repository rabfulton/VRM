# Use domovushka with an external agent

The MCP server lets a local external agent request speech, expressions and
gestures from the running companion. Use that agent’s chat interface instead of
the built-in chat window. Studio still configures the avatar, voice and library.

## Start and connect

On Arch, the optional adapter dependency is `python-mcp` (SDK 1.x, at least 1.26).
The ordinary companion does not require it. In a checkout environment, install
`requirements-mcp.txt` alongside the usual GUI requirements.

```sh
# Installed application:
domovushka --external-agent --studio

# From a checkout:
./domovushka --external-agent --studio
```

You can also choose **Speech → Conversation → External agent (MCP)** in Studio. This choice
is remembered. The built-in Chat button is disabled in this mode; the avatar’s
Chat menu action opens Studio so you can change the source. Choose Built-in chat
or pass `--builtin-chat` to return to standalone conversation.

Load an avatar and prepare the desired voice in Studio. External requests use
already prepared local model files; they do not install an engine or enable native
automatic model downloads. Custom speech backends need explicit model paths.
Speech synthesis errors remain visible through presentation status.

Configure a **stdio MCP server** in your external agent. The installed command is:

```sh
domovushka-mcp
```

For a checkout, use the absolute path to your environment’s Python, arguments
`-m vrm_overlay.mcp_server`, and set `PYTHONPATH` to the checkout directory. This
allows the agent to start the adapter from any working directory. The exact outer
configuration format depends on your MCP client. A typical server entry is:

```json
{
  "command": "/usr/bin/domovushka-mcp",
  "args": []
}
```

The adapter only attaches to a companion; it does not launch the GUI or renderer.
Its stdout is reserved for MCP. Exiting the adapter cancels its pending work and
leaves the companion running. No model API key is needed by the MCP adapter: the
external agent owns its model connection. Built-in chat credentials are separate.

## Tools

| Tool | Arguments | Result |
| --- | --- | --- |
| `avatar_capabilities` | Empty object | Mode/connection/readiness, permitted gesture and expression IDs, limits |
| `avatar_present` | Required `text`; optional `gesture`, `expression` | Queued presentation ID and state |
| `avatar_perform` | `gesture` and/or `expression` | Queued presentation without speech |
| `avatar_status` | `presentation_id` | This session’s current presentation state/error |
| `avatar_cancel` | `presentation_id` | This session’s cancelled or already terminal presentation |
| `avatar_hide` | Empty object | Request an animated exit, cancel output and hide |
| `avatar_summon` | Empty object | Interrupt an exit and replay the entrance |
| `avatar_toggle` | Empty object | Atomically toggle requested visibility |
| `avatar_visibility` | Empty object | Actual/requested visibility and lifecycle phase |

Visibility tools work in both conversation modes. Hide and summon affect the
whole companion and survive adapter disconnection. Hiding cancels all current
speech and presentations, including work from other sessions. Presentations are
rejected while hidden or exiting; explicitly summon first. These calls acknowledge
the request before its animation finishes. Query `avatar_visibility` for
`visible`, `requested_visible` and `phase` (`visible`, `entering`, `exiting`,
`hiding`, `hidden`, or `failed`). Actual visibility follows renderer snapshots
and acknowledged visibility commands. Studio and the global shortcut use this same lifecycle. See
[hide/summon controls](studio.md#hiding-and-summoning).

Input schemas reject unknown fields. Text must be nonblank and at most 4096
characters. Pass plain spoken text; inline character cues are not parsed as actions.
Gesture/expression IDs come from `avatar_capabilities`, not model filenames or
arbitrary paths. Both an animation’s library checkbox and **Available to LLM**
permission must be enabled for it to appear to an external agent. A gesture plays
one pass, respecting posture bridges and the clip’s floor-contact policy.

States are `queued`, `running`, `presenting`, `completed`, `cancelled` or `failed`.
Acceptance is immediate and does not guarantee successful synthesis. Completion
waits for audio drain and a requested gesture to finish. Expression-only requests
hold for about four seconds. Large replies should be summarized before speech.

The queue allows 16 unfinished presentations, retains at most 128 status records,
and expires unfinished work after 20 minutes. IDs are scoped to one adapter
connection and are not durable across restarts. Each adapter session can cancel
only its own requests; disconnection cancels its unfinished work. SDK request
cancellation is distinct from cancelling a presentation already accepted: use
`avatar_cancel` for that presentation ID.

Mode/model/library/voice changes or revoked action access invalidate pending work.
Manual Studio animation controls and voice previews take precedence. Studio’s
**Stop external output** cancels all external work without changing the selected
conversation source. Switching back to Built-in chat also clears external output.

## Private sockets and multiple instances

The default companion endpoint is
`$XDG_RUNTIME_DIR/domovushka/companion.sock`, with the same private per-user fallback
directory as the renderer. It is a separate API from `renderer.sock`; it supports
multiple adapters while the Qt controller remains the sole renderer client.
The socket is mode 0600 and its parent must be user-owned with mode 0700.

A custom renderer socket gets a separate presentation endpoint in the private
runtime directory. For explicit multi-instance setup, pass a private presentation
path to both processes:

```sh
domovushka --external-agent --companion-socket /path/to/private/companion.sock
domovushka-mcp --companion-socket /path/to/private/companion.sock
```

Do not substitute the renderer’s `--socket` path for this endpoint. Connections
that fail or time out are not automatically retried; an interrupted request may
already have run. At most eight adapter connections are accepted at once.

## Instructions to give your agent

You can point an agent at this section as its avatar integration guide:

- Discover `avatar_capabilities` before presenting, and refresh it after an avatar,
  voice or permissions change. Ask the user to configure Studio if it is not ready.
- Continue using your own tools for file searches, music, email and scheduled work.
  Use the avatar to present selected results; it does not provide those tools.
- Speak concise final results when useful. Do not read raw logs, credentials,
  Markdown or control syntax aloud. Use only the advertised action IDs.
- Treat a returned ID as queued work. Inspect `avatar_status` when completion
  matters, and report a failed presentation honestly. Use `avatar_cancel` to
  interrupt your previous presentation rather than enqueueing a competing reply.
- The user may change source or use local controls at any time. Respect cancelled
  work instead of repeatedly resubmitting it. Leave Studio and renderer lifecycle
  to the companion; closing your connection does not quit the application.

Automatic narration of every host response, user input from Studio into an agent,
background event delivery and scheduling require support in the external harness;
MCP presentation tools alone do not create those connections. Pointing, visual
effects, remote HTTP transport and externally supplied audio are future work.

## Verification

`make test PYTHON=/path/to/environment/bin/python` includes an official-client
stdio test when the optional SDK is available, plus fake-renderer presentation,
ownership, cancellation and source-selection checks. No model API call is used.

For an optional real-renderer check with bundled assets and an already prepared
default local voice:

```sh
python3 tests/mcp_smoke.py --backend x11
python3 tests/mcp_smoke.py --backend x11 --no-speech
```

The first command exercises actual local synthesis/playback and the second needs
no voice. Both use isolated controller settings, animation copies and sockets. Use
the Python environment containing the SDK and PySide6. The Hyprland machine can
run the same check with `--backend wayland`.
