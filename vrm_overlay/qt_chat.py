"""Studio conversation, keyring work and ordered speech/gesture coordination."""
from __future__ import annotations

from collections import deque
import json
import threading
import time

from PySide6.QtCore import QObject, Property, QTimer, Signal, Slot

from .chat import DEFAULT_CHARACTER, DEFAULT_ENDPOINT, CharacterScript, capabilities, sound_tags, system_prompt
from .credentials import CredentialError, SystemCredentials, is_local_endpoint, normalize_endpoint
from .qt_chat_network import ChatTransport


class ChatController(QObject):
    changed = Signal()
    messagesChanged = Signal()
    contextChanged = Signal()
    windowRequested = Signal()
    nearCursorRequested = Signal("QVariantMap")
    _credentialResult = Signal(int, str, str, str, str)

    def __init__(self, runtime, settings, parent=None, *, credentials=None):
        super().__init__(parent)
        self._runtime = runtime
        self._settings = settings
        self._credentials = credentials or SystemCredentials()
        self._config = dict(endpoint=DEFAULT_ENDPOINT, model="", character=DEFAULT_CHARACTER,
                            read_aloud=True, actions=True, no_key=False)
        try:
            saved = json.loads(str(settings.value("chat/config", "{}")))
            self._config = self._validated({**self._config, **saved}, require_model=False)
        except (ValueError, TypeError, AttributeError):
            pass
        self._messages = []
        self._history = []
        self._busy = False
        self._streaming = False
        self._status = "Ready"
        self._error = ""
        self._credential_busy = False
        self._credential_status = "Key not checked"
        self._credential_id = 0
        self._generation = 0
        self._closing = False
        self._events = deque()
        self._raw = ""
        self._user_text = ""
        self._available = {}
        self._script = None
        self._context = None
        self._voice_failed = False
        self._owns_speech = False
        self._speech_generation = None
        self._speech_profile = None
        self._action_pending = False
        self._next_action = 0
        self._expression = None
        self._expression_epoch = 0
        self._transport = ChatTransport(self)
        self._transport.text.connect(self._text)
        self._transport.completed.connect(self._completed)
        self._transport.failed.connect(self._failed)
        self._credentialResult.connect(self._credential_result)
        self._pump_timer = QTimer(self)
        self._pump_timer.setInterval(30)
        self._pump_timer.timeout.connect(self._pump)
        self._expression_timer = QTimer(self)
        self._expression_timer.setSingleShot(True)
        self._expression_timer.timeout.connect(self._reset_expression)
        runtime.stateChanged.connect(self.contextChanged)
        runtime.libraryChanged.connect(self.contextChanged)
        runtime.speech.changed.connect(self._speech_changed)

    config = Property("QVariantMap", lambda self: dict(self._config), notify=changed)
    messages = Property("QVariantList", lambda self: list(self._messages), notify=messagesChanged)
    busy = Property(bool, lambda self: self._busy, notify=changed)
    status = Property(str, lambda self: self._status, notify=changed)
    error = Property(str, lambda self: self._error, notify=changed)
    credentialBusy = Property(bool, lambda self: self._credential_busy, notify=changed)
    credentialStatus = Property(str, lambda self: self._credential_status, notify=changed)
    prompt = Property(str, lambda self: self._prompt(), notify=contextChanged)

    def _catalogue(self):
        if not self._config["actions"] or not self._runtime.connected:
            return {}
        return capabilities(self._runtime.state, self._runtime.animationFiles)

    def _prompt(self):
        return system_prompt(self._config["character"], self._catalogue(), sound_tags(self._runtime.speech.config))

    @staticmethod
    def _validated(values, require_model=True):
        endpoint = normalize_endpoint(values["endpoint"])
        model = values["model"].strip()
        character = values["character"].strip() or DEFAULT_CHARACTER
        if (require_model and not model) or len(model) > 200 or any(c in model for c in "\r\n\0"):
            raise ValueError("Enter the model ID provided by your chat API.")
        if len(character) > 12000 or "\0" in character:
            raise ValueError("Keep the character prompt under 12,000 characters.")
        flags = {name: values[name] for name in ("read_aloud", "actions", "no_key")}
        if any(type(value) is not bool for value in flags.values()):
            raise ValueError("Invalid chat settings.")
        if flags["no_key"] and not is_local_endpoint(endpoint):
            raise ValueError("Keyless chat is available only for a local server.")
        return dict(endpoint=endpoint, model=model, character=character, **flags)

    @Slot()
    def open(self):
        if getattr(self._runtime, 'externalAgent', False):
            self._runtime.showStudio()
            return
        self.windowRequested.emit()

    @Slot()
    def openNearCursor(self):
        if getattr(self._runtime, 'externalAgent', False):
            self.open()
            return
        from PySide6.QtGui import QCursor, QGuiApplication
        if QGuiApplication.platformName().startswith('wayland'):
            self.open()
            return
        point = QCursor.pos()
        screen = QGuiApplication.screenAt(point) or QGuiApplication.primaryScreen()
        if screen is None:
            self.open()
            return
        rect = screen.availableGeometry()
        self.nearCursorRequested.emit(dict(x=point.x(), y=point.y(), left=rect.left(),
                                          top=rect.top(), right=rect.right(), bottom=rect.bottom()))

    @Slot("QVariantMap", result=bool)
    def saveSettings(self, values):
        try:
            config = self._validated(values)
        except (ValueError, TypeError, KeyError, AttributeError) as error:
            self._error = str(error)
            self.changed.emit()
            return False
        if self._credential_busy:
            self._error = "Wait for the keyring operation to finish."
            self.changed.emit()
            return False
        if config != self._config:
            self.stop()
            # A provider/model/persona change starts a fresh conversation context.
            if any(config[key] != self._config[key] for key in ("endpoint", "model", "character")):
                self._history.clear()
                self._messages.clear()
                self.messagesChanged.emit()
            if config["endpoint"] != self._config["endpoint"]:
                self._credential_status = "Key not checked"
            self._config = config
            self._settings.setValue("chat/config", json.dumps(config))
            self._settings.sync()
        self._error = ""
        self.changed.emit()
        self.contextChanged.emit()
        return True

    def _credential_work(self, operation, secret=""):
        if self._credential_busy or self._closing:
            return
        self._credential_busy = True
        self._credential_id += 1
        job = self._credential_id
        endpoint = self._config["endpoint"]
        self._credential_status = "Waiting for system keyring…"
        self.changed.emit()
        def work():
            value = error = ""
            try:
                if operation == "save":
                    self._credentials.set(endpoint, secret)
                elif operation == "delete":
                    self._credentials.delete(endpoint)
                else:
                    value = self._credentials.get(endpoint)
            except CredentialError as problem:
                error = str(problem)
            except Exception:
                error = "The system keyring operation failed."
            try:
                self._credentialResult.emit(job, operation, endpoint, value, error)
            except RuntimeError:
                pass  # Application closed while the OS was unlocking the keyring.
        threading.Thread(target=work, name="chat-keyring", daemon=True).start()

    @Slot(str)
    def saveKey(self, secret):
        self.stop()
        self._credential_work("save", secret)

    @Slot()
    def removeKey(self):
        self.stop()
        self._credential_work("delete")

    @Slot()
    def checkKey(self):
        self._credential_work("check")

    @Slot(int, str, str, str, str)
    def _credential_result(self, job, operation, endpoint, value, error):
        if job != self._credential_id or self._closing:
            return
        self._credential_busy = False
        if endpoint != self._config["endpoint"]:
            self.changed.emit()
            return
        if error:
            self._credential_status = "System keyring unavailable"
            if operation == "send" and self._busy:
                self._failed(error)
            else:
                self._error = error
                self.changed.emit()
            return
        self._credential_status = ("Key removed" if operation == "delete" else
                                   "Key saved in system keyring" if operation == "save" else
                                   "Key stored in system keyring" if value else "No key saved for this endpoint")
        self.changed.emit()
        if operation == "send" and self._busy:
            if not value:
                self._failed("Save an API key in Chat settings, or select a keyless local server.")
            else:
                self._start_request(value)

    @Slot(str)
    def send(self, text):
        if hasattr(self._runtime, 'visibility') and not self._runtime.visibility.wanted:
            self._error = 'Summon the avatar before starting a conversation.'
            self.changed.emit()
            return
        if getattr(self._runtime, 'externalAgent', False):
            self._error = 'Use your external agent to chat, or select Built-in chat in Studio.'
            self.changed.emit()
            return
        text = text.strip()
        if self._closing or not text:
            return
        if self._credential_busy:
            self._error = "Wait for the system keyring operation to finish."
            self.changed.emit()
            return
        try:
            self._validated(self._config)
            if len(text) > 4000:
                raise ValueError("Send up to 4,000 characters at a time.")
        except ValueError as error:
            self._error = str(error)
            self.changed.emit()
            return
        self.stop()
        self._error = ""
        self._user_text = text
        self._raw = ""
        self._available = self._catalogue()
        self._script = CharacterScript(self._available, sound_tags(self._runtime.speech.config))
        self._context = (self._runtime.state["model_path"], self._runtime.animationFolder)
        self._voice_failed = False
        self._speech_profile = dict(self._runtime.speech.config)
        self._busy = True
        self._messages.extend([dict(role="user", text=text, state=""), dict(role="assistant", text="", state="Replying…")])
        self._messages = self._messages[-40:]
        self.messagesChanged.emit()
        self._status = "Requesting…" if self._config["no_key"] else "Reading API key…"
        self.changed.emit()
        if self._config["no_key"]:
            self._start_request("")
        else:
            self._credential_work("send")

    def _start_request(self, key):
        if not self._busy or self._closing:
            return
        self._streaming = True
        self._status = "Replying…"
        prompt = system_prompt(self._config["character"], self._available, self._script.tags)
        messages = [dict(role="system", content=prompt), *self._history, dict(role="user", content=self._user_text)]
        try:
            self._transport.start(self._config["endpoint"], self._config["model"], messages, key)
        except (ValueError, RuntimeError):
            self._failed("Could not start the chat request. Check the endpoint and API key.")
            return
        self._pump_timer.start()
        self.changed.emit()

    def _text(self, text):
        if not self._busy:
            return
        self._raw += text
        self._script_events(self._script.feed(text))

    def _script_events(self, events):
        for kind, value in events:
            if kind == "caption":
                self._messages[-1]["text"] += value
            elif kind == "say":
                if self._config["read_aloud"] and not self._voice_failed and self._runtime.connected and self._runtime.state["model_loaded"]:
                    self._events.append((kind, value))
            elif kind == "action":
                self._events.append((kind, value))
            elif kind == "notice":
                self._messages[-1]["state"] = value
        self.messagesChanged.emit()

    def _completed(self):
        if not self._busy:
            return
        if not self._raw.strip():
            self._failed("The model returned an empty reply.")
            return
        self._streaming = False
        self._script_events(self._script.feed("", final=True))
        self._history.extend([dict(role="user", content=self._user_text), dict(role="assistant", content=self._raw)])
        while len(self._history) > 24 or sum(len(m["content"]) for m in self._history) > 32000:
            del self._history[:2]
        self._messages[-1]["state"] = ""
        self.messagesChanged.emit()
        self._pump()

    def _speech_changed(self):
        self.contextChanged.emit()
        QTimer.singleShot(0, self._check_speech_session)

    def _check_speech_session(self):
        if self._busy and self._speech_profile != self._runtime.speech.config:
            self.stop()
            return
        # A voice/profile/preview change supersedes this chat's speech session.
        if self._busy and self._owns_speech and self._speech_generation != self._runtime.speech.generation:
            self._owns_speech = False
            if self._runtime.speech.error:
                self._voice_failed = True
                self._error = "Speech unavailable; the reply continues as text. " + self._runtime.speech.error
                self.changed.emit()
            else:
                self.stop()

    def _pump(self):
        if not self._busy or self._closing:
            return
        if self._context != (self._runtime.state["model_path"], self._runtime.animationFolder):
            self.stop()
            return
        speech = self._runtime.speech
        if self._owns_speech:
            if speech.busy:
                self._status = speech.status
                self.changed.emit()
                return
            self._owns_speech = False
            if speech.error:
                self._voice_failed = True
                self._error = "Speech unavailable; the reply continues as text. " + speech.error
        if self._action_pending or speech.busy:
            return
        while self._events:
            kind, value = self._events[0]
            if kind == "action" and time.monotonic() < self._next_action:
                return
            self._events.popleft()
            if kind == "say":
                if self._voice_failed or not self._runtime.connected:
                    continue
                speech.speak(value)
                self._speech_generation = speech.generation
                self._owns_speech = True
                return
            if kind == "action":
                self._perform(value)
                return
        if not self._streaming:
            self._busy = False
            self._status = "Ready"
            self._pump_timer.stop()
        else:
            self._status = "Replying…"
        self.changed.emit()

    def _current_context(self):
        return self._runtime.connected and self._context == (self._runtime.state["model_path"], self._runtime.animationFolder)

    def _perform(self, key):
        capability = self._available.get(key)
        if capability is None or not self._current_context():
            return
        token = self._generation
        current = lambda: token == self._generation and self._current_context() and not self._closing
        commands = list(capability.commands)
        if capability.kind == "expression":
            self._expression_timer.stop()
            if self._expression:
                commands.insert(0, dict(action="set_expression", name=self._expression[0], weight=0.0))
            self._expression_epoch += 1
            self._expression = (capability.commands[0]["name"], self._context[0]) if capability.commands else None
        if not commands:
            return
        self._action_pending = True
        self._next_action = time.monotonic() + 0.75
        def done(_reply):
            if current():
                self._action_pending = False
                if capability.kind == "expression" and self._expression:
                    self._expression_timer.start(4000)
        def failed(_message):
            if current():
                self._action_pending = False
                self._error = "The avatar could not perform a character cue."
                self.changed.emit()
        if capability.kind == "animation" and hasattr(self._runtime, "queueCharacterAnimation"):
            self._runtime.queueCharacterAnimation(commands, "Character " + capability.name, done=done, failed=failed, guard=current)
        else:
            self._runtime._queue(commands, "Character " + capability.name, done=done, failed=failed, guard=current)

    def _reset_expression(self):
        self._expression_timer.stop()
        expression, self._expression = self._expression, None
        self._expression_epoch += 1
        epoch = self._expression_epoch
        if expression and self._runtime.connected and self._runtime.state["model_path"] == expression[1]:
            self._runtime._queue([dict(action="set_expression", name=expression[0], weight=0.0)], "Release character expression",
                                 guard=lambda: epoch == self._expression_epoch and self._runtime.connected and self._runtime.state["model_path"] == expression[1])

    def _failed(self, message):
        if not self._busy:
            return
        self.stop()
        self._error = message
        self._status = "Chat unavailable"
        if self._messages and self._messages[-1]["role"] == "assistant":
            self._messages[-1]["state"] = "Reply interrupted"
            self.messagesChanged.emit()
        self.changed.emit()

    @Slot()
    def stop(self):
        was_busy = self._busy
        self._generation += 1
        self._transport.cancel()
        self._pump_timer.stop()
        self._events.clear()
        self._action_pending = False
        self._busy = self._streaming = False
        owned, self._owns_speech = self._owns_speech, False
        if owned:
            self._runtime.speech.stop()
        self._reset_expression()
        if was_busy and self._messages and self._messages[-1]["role"] == "assistant":
            self._messages[-1]["state"] = "Stopped"
            self.messagesChanged.emit()
        self._status = "Stopped" if was_busy else "Ready"
        self.changed.emit()

    @Slot()
    def newConversation(self):
        self.stop()
        self._history.clear()
        self._messages.clear()
        self._error = ""
        self.messagesChanged.emit()
        self.changed.emit()

    def shutdown(self):
        self.stop()
        self._closing = True
        self._credential_id += 1
