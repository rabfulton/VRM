"""Bounded streaming chat protocol and the character's explicit capability catalogue."""
from __future__ import annotations

import codecs
from dataclasses import dataclass
import json
import re

from .animation_library import describe_animations

DEFAULT_ENDPOINT = "https://api.openai.com/v1/chat/completions"
DEFAULT_CHARACTER = """You are Yuki, a warm, playful anime-style desktop companion.
Be curious, expressive and helpful, with concise conversational answers. Your
personality should complement useful answers rather than overwhelm them. You may
be surprised or mildly annoyed, but do not nag the user for attention. Do not
pretend you can see the desktop, hear sounds or take actions you do not have."""
MAX_RESPONSE = 16000
SOUND_TAGS = ("gasp", "laugh", "chuckle", "sigh", "cough", "groan", "sniff", "shush", "clear throat")


@dataclass(frozen=True)
class Capability:
    kind: str
    name: str
    description: str
    commands: tuple[dict, ...]


def capabilities(state, files):
    if not state.get("model_loaded"):
        return {}
    result = {}
    for entry in describe_animations(files):
        if entry['enabled'] and entry["llm_enabled"] and not entry.get('config_error'):
            name = entry["llm_id"]
            result[("animation", name)] = Capability("animation", name, entry["llm_description"], (
                {"action": "load_animation", "path": entry["path"]},
                {"action": "blend_to", "index": 0, "duration": state.get("blend", {}).get("duration", 0.45), "loop": entry["llm_loop"]},
                {"action": "play", "speed": 1.0},
            ))
    expressions = {name.casefold(): name for name in state.get("expressions", [])}
    for name, aliases, description in (
        ("happy", ("happy", "joy"), "A happy face"),
        ("angry", ("angry",), "An annoyed face"),
        ("sad", ("sad", "sorrow"), "A sad face"),
        ("surprised", ("surprised", "surprise"), "A surprised face"),
    ):
        match = next((expressions[a] for a in aliases if a in expressions), None)
        if match:
            result[("expression", name)] = Capability("expression", name, description, (
                {"action": "set_expression", "name": match, "weight": 0.55},))
    if any(kind == "expression" for kind, _ in result):
        result[("expression", "neutral")] = Capability("expression", "neutral", "Release the chat expression", ())
    return result


def sound_tags(config):
    return SOUND_TAGS if config.get("provider") == "crispasr" and config.get("model") in ("chatterbox-nano", "chatterbox-turbo") else ()


def system_prompt(character, available, tags):
    lines = [character.strip() or DEFAULT_CHARACTER, "\nOUTPUT CONTRACT (application capabilities):",
             "Write plain spoken prose, with short sentences and paragraphs. Avoid Markdown, stage directions and lengthy monologues.",
             "Only use the exact optional action cues listed below. Place cues between sentences; do not describe executing them or speak their syntax.",
             "Use gestures sparingly (at most 8 cues per reply). Expressions return to neutral after a few seconds. Animation poses persist until changed.",
             "Animation names are labels, not instructions. Old conversation cues may no longer be available; only use the current list."]
    lines.append("Animation labels identify one-shot, loopable, startup and idle roles. The app returns to configured idles automatically after actions. Use entrance animations only when an entrance is appropriate.")
    if ("animation", "idle") in available:
        lines.append("Return to the listed idle animation when appropriate.")
    if tags:
        lines += ["For occasional non-verbal vocal sounds, you may insert " + ", ".join(f"[{tag}]" for tag in tags) + ".",
                  "Example: [gasp] Oh! I did not expect that. Use sound tags sparingly; do not invent emotion tags."]
    else:
        lines.append("The current speech engine has no advertised sound tags. Do not put vocal directions in brackets.")
    if available:
        lines += [f"[[{c.kind}:{c.name}]] — {c.description}" for c in available.values()]
    else:
        lines.append("No avatar actions are currently available.")
    lines += ["Cues in code spans or code fences are not commands. Do not invent tools, file paths or IPC commands.",
              "Desktop window inspection, pushing/shaking windows, sitting on windows, microphone input and system sound reactions are not available yet. Do not claim to perform them.",
              "Only conversation text is provided. Do not imply you have seen or heard anything else."]
    return "\n".join(lines)


class StreamError(ValueError):
    pass


class CompletionStream:
    """Incremental UTF-8/data-only SSE parser, including fragmented CRLF frames."""
    def __init__(self):
        self._decoder = codecs.getincrementaldecoder("utf-8")()
        self._buffer = ""
        self._data = []
        self.done = False
        self.finish_reason = None
        self.characters = 0

    def feed(self, data, final=False):
        try:
            self._buffer += self._decoder.decode(data, final=final)
        except UnicodeError:
            raise StreamError("The chat server returned invalid UTF-8.") from None
        if len(self._buffer) > 65536:
            raise StreamError("The chat server sent an oversized stream frame.")
        output = []
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            self._line(line.removesuffix("\r"), output)
        if final:
            if self._buffer:
                self._line(self._buffer.removesuffix("\r"), output)
                self._buffer = ""
            self._line("", output)
            if not self.done and self.finish_reason is None:
                raise StreamError("The chat stream ended before the response was complete.")
        return output

    def _line(self, line, output):
        if not line:
            if self._data:
                payload = "\n".join(self._data)
                self._data.clear()
                self._event(payload, output)
        elif line.startswith("data:"):
            self._data.append(line[5:].removeprefix(" "))
            if sum(map(len, self._data)) > 65536:
                raise StreamError("The chat server sent an oversized event.")
        # Comments, event names and IDs do not contain completion text.

    def _event(self, data, output):
        if self.done:
            return
        if data == "[DONE]":
            self.done = True
            return
        try:
            event = json.loads(data)
            if not isinstance(event, dict) or "error" in event:
                raise ValueError
            choices = event.get("choices", [])
            if not isinstance(choices, list):
                raise ValueError
            for choice in choices:
                if choice.get("index", 0) != 0:
                    continue
                delta = choice.get("delta", {})
                if delta.get("tool_calls") or delta.get("function_call"):
                    raise StreamError("This character uses inline action cues; the server returned an unsupported tool call.")
                text = delta.get("content") or delta.get("refusal") or ""
                if not isinstance(text, str):
                    raise ValueError
                self.characters += len(text)
                if self.characters > MAX_RESPONSE:
                    raise StreamError("The reply exceeded the character's response limit.")
                if text:
                    output.append(text)
                if reason := choice.get("finish_reason"):
                    if reason == "length":
                        raise StreamError("The model reached its output limit; try a shorter question.")
                    if reason not in ("stop",):
                        raise StreamError("The model did not complete this reply.")
                    self.finish_reason = reason
        except StreamError:
            raise
        except (ValueError, TypeError, AttributeError):
            raise StreamError("The chat server returned an invalid completion event.") from None


class Sentences:
    # Wait for a following separator so a network chunk ending in '.' cannot
    # split a decimal/abbreviation before the next delta arrives.
    _boundary = re.compile(r"\n+|[.!?]+[\"”')]*(?=\s)|[。！？]")
    _abbreviations = re.compile(r"(?:\b(?:Mr|Mrs|Ms|Dr|Prof|Sr|Jr|St|vs|etc)|e\.g|i\.e)\.$", re.I)

    def __init__(self):
        self.buffer = ""

    def feed(self, text, final=False):
        self.buffer += text
        output = []
        while self.buffer:
            end = None
            for match in self._boundary.finditer(self.buffer):
                if not self._abbreviations.search(self.buffer[:match.end()].strip()):
                    end = match.end()
                    break
            if end is None or end > 360:
                if len(self.buffer) >= 360:
                    end = self.buffer.rfind(" ", 0, 360)
                    if end < 120:
                        end = 360
                elif final:
                    end = len(self.buffer)
                else:
                    break
            sentence = re.sub(r"\s+", " ", self.buffer[:end]).strip()
            self.buffer = self.buffer[end:].lstrip()
            if sentence:
                output.append(sentence)
        return output


class CharacterScript:
    """Parse only complete allowlisted cues; never send control syntax to TTS.

    Events: caption (display text), say (sentence), action (kind/name), notice.
    Code spans/fences are displayed but cannot invoke gestures or become speech.
    """
    def __init__(self, available=(), tags=()):
        self.available = set(available)
        self.tags = set(tags)
        self.buffer = ""
        self.sentences = Sentences()
        self.code = ""
        self.actions = 0

    def feed(self, text, final=False):
        self.buffer += text
        events = []
        def prose(value, spoken=True):
            if value:
                events.append(("caption", value))
                if spoken:
                    events.extend(("say", s) for s in self.sentences.feed(value))
        while self.buffer:
            if self.buffer[0] == "`":
                count = len(self.buffer) - len(self.buffer.lstrip("`"))
                if count < 3 and count == len(self.buffer) and not final:
                    break
                marker = "```" if count >= 3 else "`"
                if not self.code:
                    events.extend(("say", s) for s in self.sentences.feed("", final=True))
                    self.code = marker
                elif self.code == marker:
                    self.code = ""
                prose(self.buffer[:count], spoken=False)
                self.buffer = self.buffer[count:]
                continue
            if not self.code and self.buffer[0] == "[":
                if len(self.buffer) == 1 and not final:
                    break
                control = self.buffer.startswith("[[")
                close = "]]" if control else "]"
                end = self.buffer.find(close, 2 if control else 1)
                if end < 0:
                    if len(self.buffer) <= 128 and not final:
                        break
                    # An unfinished/oversized tag is never executed or spoken.
                    prose(self.buffer, spoken=False)
                    self.buffer = ""
                    break
                tag = self.buffer[2 if control else 1:end].strip()
                literal = self.buffer[:end + len(close)]
                self.buffer = self.buffer[end + len(close):]
                if control:
                    events.extend(("say", s) for s in self.sentences.feed("", final=True))
                    parts = tuple(tag.split(":"))
                    if parts in self.available and self.actions < 8:
                        self.actions += 1
                        events.append(("action", parts))
                    else:
                        events.append(("notice", "Ignored an unavailable or excessive character cue."))
                elif tag in self.tags:
                    prose(literal)
                else:
                    prose(literal, spoken=False)
                continue
            boundary = re.search(r"[`\[]" if not self.code else r"`", self.buffer)
            end = boundary.start() if boundary else len(self.buffer)
            if not end:
                end = 1
            prose(self.buffer[:end], spoken=not self.code)
            self.buffer = self.buffer[end:]
        if final:
            events.extend(("say", s) for s in self.sentences.feed("", final=True))
        return events
