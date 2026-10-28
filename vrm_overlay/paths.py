"""Checkout/installed resources and Linux user paths; no Qt dependency."""
from __future__ import annotations

import os
from pathlib import Path
import pwd
import shutil
import stat


def checkout_root():
    root = Path(__file__).resolve().parent.parent
    return root if (root / 'src/main.c').is_file() and (root / 'Makefile').is_file() else None


def user_directory(variable, fallback):
    value = os.environ.get(variable, '')
    if value.startswith('/'):
        return Path(value)
    home = os.environ.get('HOME', '')
    return Path(home if home.startswith('/') else pwd.getpwuid(os.getuid()).pw_dir) / fallback


def data_home():
    return user_directory('XDG_DATA_HOME', '.local/share')


def settings_path():
    override = os.environ.get('VRM_SETTINGS_PATH')
    return Path(override).expanduser().absolute() if override else user_directory('XDG_CONFIG_HOME', '.config') / 'domovushka/renderer.cfg'


def _private_directory(path, create):
    if create:
        try:
            path.mkdir(mode=0o700)
        except FileExistsError:
            pass
    try:
        info = path.lstat()
    except FileNotFoundError:
        return not create
    return stat.S_ISDIR(info.st_mode) and info.st_uid == os.getuid() and not info.st_mode & 0o077


def default_socket_path(*, create=True):
    runtime = os.environ.get('XDG_RUNTIME_DIR', '')
    if runtime.startswith('/') and Path(runtime).is_dir() and _private_directory(Path(runtime), False):
        folder = Path(runtime) / 'domovushka'
    else:
        folder = Path('/tmp') / f'domovushka-{os.getuid()}'
    if create and not _private_directory(folder, True):
        raise RuntimeError(f'Runtime directory must be owned by you with mode 0700: {folder}')
    socket = str(folder / 'renderer.sock')
    if create and len(os.fsencode(socket)) >= 108:
        raise RuntimeError('Runtime socket path is too long; use --socket with a shorter private path')
    return socket


def resource_directory():
    override = os.environ.get('VRM_DATA_DIR')
    if override:
        return Path(override).expanduser().resolve()
    root = checkout_root()
    if root:
        return root
    from ._installed import DATA_DIR
    return Path(DATA_DIR)


def renderer_executable():
    override = os.environ.get('VRM_RENDERER')
    if override:
        return shutil.which(override) or override
    root = checkout_root()
    if root:
        return str(root / 'vrm-overlay')
    from ._installed import BIN_DIR
    binary = Path(BIN_DIR) / 'vrm-overlay'
    return str(binary) if binary.is_file() else shutil.which('vrm-overlay') or str(binary)


def development_file(name):
    root = checkout_root()
    return root / name if root else None
