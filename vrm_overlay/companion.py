"""Bounded local presentation API shared by MCP adapters and the Qt companion."""
from __future__ import annotations

import json
import math
import hashlib
import os
from pathlib import Path
import socket
import threading
import time

from .paths import default_socket_path

MAX_MESSAGE = 65536
MAX_RESPONSE = 1024 * 1024


def companion_socket_path(renderer_socket=None, *, create=False):
    default = default_socket_path(create=create)
    name = ('companion.sock' if renderer_socket is None or renderer_socket == default else
            'companion-' + hashlib.sha256(os.fsencode(os.path.abspath(renderer_socket))).hexdigest()[:16] + '.sock')
    path = str(Path(default).parent / name)
    if len(os.fsencode(path)) >= 108:
        raise ValueError('Companion socket path is too long; choose --companion-socket explicitly.')
    return path


def schema(properties=None, required=()):
    return dict(type='object', properties=properties or {}, required=list(required), additionalProperties=False)


IDENTIFIER = dict(type='string', minLength=1, maxLength=128)
TOOLS = [
    dict(name='avatar_walk', description='Walk a standing humanoid horizontally: supply direction (left/right) and optional relative distance (default 240), OR absolute x for the avatar viewport centre. Screen units are X11 desktop pixels or Wayland output logical coordinates. Clamps to the current workarea. Returns a motion ID and duration estimate; poll avatar_motion_status for completion after settling into idle. External-agent mode required.',
         inputSchema=dict(schema(dict(direction=dict(type='string', enum=['left', 'right'], maxLength=5), distance=dict(type='number', minimum=24, maximum=1600, default=240), x=dict(type='number', minimum=-131072, maximum=131072))),
                          oneOf=[dict(required=['direction'], **{'not':dict(required=['x'])}),
                                 dict(required=['x'], **{'not':dict(anyOf=[dict(required=['direction']),dict(required=['distance'])])})])),
    dict(name='avatar_motion_status', description='Read a walking motion owned by this agent session: phase, progress, distance, boundary clamping, estimated duration and remaining time. Only completed/cancelled/failed are terminal; duration estimates are not completion signals.',
         inputSchema=schema(dict(motion_id=IDENTIFIER), ['motion_id'])),
    dict(name='avatar_motion_cancel', description='Stop a walking motion owned by this agent session and return to standing idle. Poll avatar_motion_status until terminal.',
         inputSchema=schema(dict(motion_id=IDENTIFIER), ['motion_id'])),
    dict(name='avatar_hide', description='Hide the companion after its optional exit animation, cancelling current speech and presentations. Available in either conversation mode. Returns the requested visibility and phase.', inputSchema=schema()),
    dict(name='avatar_summon', description='Show the companion and replay its entrance animation. Interrupts an in-progress exit. Available in either conversation mode.', inputSchema=schema()),
    dict(name='avatar_toggle', description='Atomically toggle requested companion visibility using the same hide/summon lifecycle as Studio and the global shortcut.', inputSchema=schema()),
    dict(name='avatar_visibility', description='Read actual and requested visibility and the current hide/summon phase.', inputSchema=schema()),
    dict(name='avatar_capabilities', description='Discover whether external-agent output is enabled, speech readiness, and permitted gesture/expression IDs. No file paths or credentials are returned.', inputSchema=schema()),
    dict(name='avatar_present', description='Queue plain text for local speech with an optional permitted gesture and expression. Returns a presentation ID, not a playback-completion acknowledgement. Prepare the voice in Studio first.',
         inputSchema=schema(dict(text=dict(type='string', minLength=1, maxLength=4096), gesture=IDENTIFIER, expression=IDENTIFIER), ['text'])),
    dict(name='avatar_perform', description='Queue one pass of a permitted gesture and/or an expression, without speech. At least one action ID is required.',
         inputSchema=schema(dict(gesture=IDENTIFIER, expression=IDENTIFIER))),
    dict(name='avatar_status', description='Read the state of a presentation created in this agent session. Completion means speech has drained and a requested gesture has finished.',
         inputSchema=schema(dict(presentation_id=IDENTIFIER), ['presentation_id'])),
    dict(name='avatar_cancel', description='Cancel a presentation belonging to this agent session, including its owned speech and queued cues.',
         inputSchema=schema(dict(presentation_id=IDENTIFIER), ['presentation_id'])),
]


def validate_call(name, arguments):
    definition = next((tool for tool in TOOLS if tool['name'] == name), None)
    if definition is None:
        raise ValueError('Unknown avatar tool.')
    if not isinstance(arguments, dict):
        raise ValueError('Tool arguments must be an object.')
    spec = definition['inputSchema']
    if set(arguments) - set(spec['properties']) or set(spec['required']) - set(arguments):
        raise ValueError('Unexpected or missing tool arguments.')
    for key, value in arguments.items():
        limits = spec['properties'][key]
        valid = ((isinstance(value, str) and '\0' not in value and limits.get('minLength', 0) <= len(value) <= limits['maxLength']
                  and ('enum' not in limits or value in limits['enum'])) if limits['type'] == 'string' else
                 (type(value) in (int, float) and limits['minimum'] <= value <= limits['maximum'] and math.isfinite(value)))
        if not valid:
            raise ValueError(f'Invalid {key}.')
    if name == 'avatar_present' and not arguments['text'].strip():
        raise ValueError('Speech text cannot be blank.')
    if name == 'avatar_perform' and not arguments:
        raise ValueError('Provide a gesture or expression ID.')
    if name == 'avatar_walk' and not (('direction' in arguments and 'x' not in arguments) or
                                     ('x' in arguments and 'direction' not in arguments and 'distance' not in arguments)):
        raise ValueError('Provide direction and optional distance, or absolute x; do not mix them.')


class CompanionRejection(ValueError):
    pass


class CompanionClient:
    """One persistent connection identifies one agent; no side-effect retries."""
    def __init__(self, path=None):
        self.path = path or companion_socket_path()
        self.socket = None
        self.lock = threading.RLock()
        self.buffer = bytearray()
        self.next_id = 1

    def call(self, name, arguments):
        validate_call(name, arguments)
        with self.lock:
            try:
                if self.socket is None:
                    self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    self.socket.settimeout(3)
                    self.socket.connect(self.path)
                request_id = self.next_id
                self.next_id += 1
                payload = json.dumps(dict(id=request_id, tool=name, arguments=arguments), allow_nan=False).encode() + b'\n'
                if len(payload) > MAX_MESSAGE:
                    raise ValueError('Presentation request is too large.')
                self.socket.sendall(payload)
                deadline = time.monotonic() + 3
                while b'\n' not in self.buffer:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError('Companion response timed out.')
                    self.socket.settimeout(remaining)
                    chunk = self.socket.recv(65536)
                    if not chunk:
                        raise ConnectionError('Companion disconnected.')
                    self.buffer.extend(chunk)
                    if len(self.buffer) > MAX_RESPONSE:
                        raise ValueError('Companion response exceeded its limit.')
                line, _, rest = self.buffer.partition(b'\n')
                self.buffer = bytearray(rest)
                response = json.loads(line)
                if not isinstance(response, dict) or response.get('id') != request_id:
                    raise ValueError('Invalid companion response.')
                if 'error' in response:
                    raise CompanionRejection(response['error'])
                return response['result']
            except OSError as error:
                self.close()
                raise ConnectionError('Cannot reach the companion. Start domovushka first. Requests are not retried.') from error
            except CompanionRejection:
                # Application rejections leave the session intact, retaining its job IDs.
                raise
            except (ValueError, KeyError, TypeError):
                self.close()
                raise ValueError('Invalid companion response; the session was closed.') from None

    def close(self):
        with self.lock:
            if self.socket is not None:
                self.socket.close()
                self.socket = None
            self.buffer.clear()
