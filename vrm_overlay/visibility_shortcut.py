"""Isolated global shortcut helper: XGrabKey on Xorg, runtime binds on Hyprland.

The parent owns stdin; EOF or SIGTERM releases the registration. No Qt or MCP
SDK is required here. Compositor configuration files are never written.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import select
import shlex
import signal
import socket
import subprocess
import sys

MODIFIERS = {'ctrl': ('CTRL', 4), 'control': ('CTRL', 4), 'alt': ('ALT', 8),
             'shift': ('SHIFT', 1), 'super': ('SUPER', 64), 'meta': ('SUPER', 64)}
KEYS = {'space': 'space', 'tab': 'Tab', 'escape': 'Escape', 'esc': 'Escape',
        'return': 'Return', 'enter': 'Return', 'home': 'Home', 'end': 'End',
        'insert': 'Insert', 'delete': 'Delete', 'left': 'Left', 'right': 'Right',
        'up': 'Up', 'down': 'Down', 'pageup': 'Prior', 'pagedown': 'Next'}


def parse_shortcut(text):
    parts = [part.strip().lower() for part in text.split('+')]
    if len(parts) < 2 or not all(parts) or any(p not in MODIFIERS for p in parts[:-1]):
        raise ValueError('Use one chord, such as Ctrl+Alt+Space or Super+F9.')
    mods = [MODIFIERS[p] for p in parts[:-1]]
    if len({mask for _, mask in mods}) != len(mods):
        raise ValueError('Each modifier may appear only once.')
    key = KEYS.get(parts[-1])
    if re.fullmatch(r'[a-z0-9]|f(?:[1-9]|[12][0-9]|3[0-5])', parts[-1]):
        key = parts[-1].upper() if parts[-1].startswith('f') and len(parts[-1]) > 1 else parts[-1]
    if key is None:
        raise ValueError('Use a letter, digit, F1–F35, Space, Tab, Escape, Return, arrow or navigation key.')
    return '+'.join([name.title() for name, _ in mods] + [key]), sum(mask for _, mask in mods), key


def emit(event, **values):
    print(json.dumps(dict(event=event, **values)), flush=True)


def lua_string(value):
    # Lua decimal byte escapes also handle unusual filenames and UTF-8 safely.
    return '"' + ''.join('\\' + format(byte, '03d') for byte in value.encode('utf-8')) + '"'


class XKeyEvent(C.Structure):
    _fields_ = [('type', C.c_int), ('serial', C.c_ulong), ('send_event', C.c_int),
                ('display', C.c_void_p), ('window', C.c_ulong), ('root', C.c_ulong),
                ('subwindow', C.c_ulong), ('time', C.c_ulong), ('x', C.c_int), ('y', C.c_int),
                ('x_root', C.c_int), ('y_root', C.c_int), ('state', C.c_uint),
                ('keycode', C.c_uint), ('same_screen', C.c_int)]


class XEvent(C.Union):
    _fields_ = [('type', C.c_int), ('key', XKeyEvent), ('pad', C.c_long * 24)]


class XError(C.Structure):
    _fields_ = [('type', C.c_int), ('display', C.c_void_p), ('resourceid', C.c_ulong),
                ('serial', C.c_ulong), ('error_code', C.c_ubyte),
                ('request_code', C.c_ubyte), ('minor_code', C.c_ubyte)]


class ModifierMap(C.Structure):
    _fields_ = [('max_keypermod', C.c_int), ('modifiermap', C.POINTER(C.c_ubyte))]


def x11_shortcut(mask, key):
    x = C.CDLL('libX11.so.6')
    signatures = {
        'XOpenDisplay': (C.c_void_p, [C.c_char_p]),
        'XDefaultRootWindow': (C.c_ulong, [C.c_void_p]),
        'XConnectionNumber': (C.c_int, [C.c_void_p]),
        'XStringToKeysym': (C.c_ulong, [C.c_char_p]),
        'XKeysymToKeycode': (C.c_ubyte, [C.c_void_p, C.c_ulong]),
        'XGrabKey': (C.c_int, [C.c_void_p, C.c_int, C.c_uint, C.c_ulong, C.c_int, C.c_int, C.c_int]),
        'XRefreshKeyboardMapping': (C.c_int, [C.POINTER(XEvent)]),
        'XSync': (C.c_int, [C.c_void_p, C.c_int]),
        'XCloseDisplay': (C.c_int, [C.c_void_p]),
        'XNextEvent': (C.c_int, [C.c_void_p, C.POINTER(XEvent)]),
        'XPeekEvent': (C.c_int, [C.c_void_p, C.POINTER(XEvent)]),
        'XPending': (C.c_int, [C.c_void_p]),
        'XGetModifierMapping': (C.POINTER(ModifierMap), [C.c_void_p]),
        'XFreeModifiermap': (C.c_int, [C.POINTER(ModifierMap)]),
        'XkbSetDetectableAutoRepeat': (C.c_int, [C.c_void_p, C.c_int, C.POINTER(C.c_int)]),
    }
    for name, (result, args) in signatures.items():
        getattr(x, name).restype, getattr(x, name).argtypes = result, args
    errors = []
    handler_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(XError))
    handler = handler_type(lambda _display, error: errors.append(error.contents.error_code) or 0)
    x.XSetErrorHandler.argtypes = [handler_type]
    x.XSetErrorHandler(handler)  # Isolated process; never replaces Qt's handler.
    display = x.XOpenDisplay(None)
    if not display:
        raise ValueError('Cannot open the X11 display.')
    try:
        code = x.XKeysymToKeycode(display, x.XStringToKeysym(key.encode()))
        if not code:
            raise ValueError('This key is unavailable in the current keyboard layout.')
        locks = {2}  # Caps Lock
        mapping = x.XGetModifierMapping(display)
        if not mapping:
            raise ValueError('Cannot read the X11 keyboard modifier map.')
        try:
            lock_codes = {x.XKeysymToKeycode(display, x.XStringToKeysym(k)) for k in (b'Num_Lock', b'Scroll_Lock')}
            lock_codes.discard(0)
            for modifier in range(8):
                if any(mapping.contents.modifiermap[modifier * mapping.contents.max_keypermod + i] in lock_codes
                       for i in range(mapping.contents.max_keypermod)):
                    locks.add(1 << modifier)
        finally:
            x.XFreeModifiermap(mapping)
        for count in range(len(locks) + 1):
            for subset in itertools.combinations(locks, count):
                x.XGrabKey(display, code, mask | sum(subset), x.XDefaultRootWindow(display), False, 1, 1)
        x.XSync(display, False)
        if errors:
            raise ValueError('Shortcut is already in use or X11 refused the key grab. Choose another chord.')
        supported = C.c_int()
        x.XkbSetDetectableAutoRepeat(display, True, C.byref(supported))
        emit('ready', backend='X11')
        pressed = False
        while True:
            readable, _, _ = select.select([sys.stdin, x.XConnectionNumber(display)], [], [], 1)
            if sys.stdin in readable and not os.read(sys.stdin.fileno(), 4096):
                return
            while x.XPending(display):
                event = XEvent()
                x.XNextEvent(display, C.byref(event))
                if event.type == 34:
                    # XTest and some servers send a harmless initial mapping
                    # notification. Refresh before comparing the actual key.
                    x.XRefreshKeyboardMapping(C.byref(event))
                    if x.XKeysymToKeycode(display, x.XStringToKeysym(key.encode())) != code:
                        raise ValueError('Keyboard layout changed. Apply the shortcut again in Studio.')
                    continue
                if event.type == 2 and not pressed:
                    pressed = True
                    emit('activated')
                elif event.type == 3:
                    if not supported.value and x.XPending(display):
                        next_event = XEvent()
                        x.XPeekEvent(display, C.byref(next_event))
                        if next_event.type == 2 and next_event.key.time == event.key.time and next_event.key.keycode == event.key.keycode:
                            x.XNextEvent(display, C.byref(next_event))
                            continue
                    pressed = False
    finally:
        x.XCloseDisplay(display)  # Releases every grab, including partial failures.


def hyprctl(*arguments):
    result = subprocess.run(['hyprctl', *arguments], capture_output=True, text=True, timeout=3)
    if result.returncode or result.stdout.strip().lower().startswith(('error', 'unknown', 'invalid')):
        raise ValueError((result.stderr.strip() or result.stdout.strip() or 'hyprctl failed')[:400])
    return result.stdout.strip()


class HyprlandBinding:
    def __init__(self, mask, key, companion_socket):
        self.mask, self.key = mask, key
        self.mods = ' '.join(name for name, bit in [('SHIFT', 1), ('CTRL', 4), ('ALT', 8), ('SUPER', 64)] if mask & bit)
        self.marker = 'domovushka-' + hashlib.sha256(os.fsencode(companion_socket)).hexdigest()[:16]
        self.variable = '_G[' + json.dumps('__' + self.marker) + ']'
        bootstrap = ('import sys;sys.path.insert(0,' + repr(str(Path(__file__).resolve().parent.parent)) + ');'
                     'from vrm_overlay.gui import main;raise SystemExit(main())')
        self.command = shlex.join([sys.executable, '-c', bootstrap, '--toggle-avatar', '--companion-socket', companion_socket])
        self.lua = False
        self.installed = False

    def matches(self):
        binds = json.loads(hyprctl('-j', 'binds'))
        if not isinstance(binds, list):
            raise ValueError('Hyprland returned an invalid bindings list.')
        return [b for b in binds if b.get('modmask') == self.mask and str(b.get('key', '')).casefold() == self.key.casefold()]

    def remove(self):
        if not self.installed:
            return
        if self.lua:
            hyprctl('eval', f'if {self.variable} then {self.variable}:remove(); {self.variable}=nil end')
        else:
            matches = self.matches()
            # Hyprlang unbind removes all matches: never remove someone else's.
            if matches and all(b.get('description') == self.marker and b.get('arg') == self.command for b in matches):
                hyprctl('keyword', 'unbind', f'{self.mods},{self.key}')
        self.installed = False

    def install(self):
        matches = self.matches()
        if any(b.get('description') != self.marker for b in matches):
            raise ValueError('This shortcut is already bound in Hyprland. Choose another chord.')
        try:
            hyprctl('eval', 'assert(hl and hl.bind)')
            self.lua = True
        except ValueError:
            self.lua = False
        if self.lua:
            # Keep the returned handle; removing it cannot unbind another app.
            chord = ' + '.join(self.mods.split() + [self.key])
            code = (f'if {self.variable} then {self.variable}:remove() end; '
                    f'{self.variable}=hl.bind({lua_string(chord)},hl.dsp.exec_cmd({lua_string(self.command)}),'
                    f'{{description={json.dumps(self.marker)}}})')
            hyprctl('eval', code)
        else:
            if matches:
                if not all(b.get('arg') == self.command for b in matches):
                    raise ValueError('A different companion owns this shortcut.')
                hyprctl('keyword', 'unbind', f'{self.mods},{self.key}')
            hyprctl('keyword', 'bindd', f'{self.mods},{self.key},{self.marker},exec,{self.command}')
        self.installed = True


def hyprland_shortcut(mask, key, companion_socket):
    signature = os.environ.get('HYPRLAND_INSTANCE_SIGNATURE', '')
    if not signature or '/' in signature:
        raise ValueError('Global Wayland shortcuts currently require Hyprland and hyprctl.')
    paths = [Path(os.environ.get('XDG_RUNTIME_DIR', '/run/user/' + str(os.getuid()))) / 'hypr' / signature / '.socket2.sock',
             Path('/tmp/hypr') / signature / '.socket2.sock']
    events = socket.socket(socket.AF_UNIX)
    binding = HyprlandBinding(mask, key, companion_socket)
    try:
        events.connect(str(next((p for p in paths if p.exists()), paths[0])))
        binding.install()
        emit('ready', backend='Hyprland')
        buffer = b''
        while True:
            readable, _, _ = select.select([sys.stdin, events], [], [])
            if sys.stdin in readable and not os.read(sys.stdin.fileno(), 4096):
                return
            if events in readable:
                chunk = events.recv(4096)
                if not chunk:
                    raise ValueError('Hyprland disconnected. Apply the shortcut again after reconnecting.')
                buffer += chunk
                if len(buffer) > 65536:
                    raise ValueError('Oversized Hyprland event.')
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    if line.startswith(b'configreloaded>>'):
                        binding.install()
    finally:
        events.close()
        try:
            binding.remove()
        except (OSError, ValueError, subprocess.TimeoutExpired):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('shortcut')
    parser.add_argument('--companion-socket', required=True)
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, lambda *_args: sys.exit(0))
    try:
        _, mask, key = parse_shortcut(args.shortcut)
        if os.environ.get('WAYLAND_DISPLAY') or os.environ.get('XDG_SESSION_TYPE') == 'wayland':
            hyprland_shortcut(mask, key, args.companion_socket)
        else:
            x11_shortcut(mask, key)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        emit('error', message=str(error))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
