"""Behaviour flags in the same sidecar .cfg used by native animation tuning."""
from __future__ import annotations

import os
from pathlib import Path
import stat
import tempfile

FLAGS = ("loopable", "startup", "is_idle")


def default_properties(name):
    name = name.casefold()
    return dict(loopable=name in ("relax", "thinking"),
                startup=name == "enter_from_left", is_idle=name == "relax")


def load_properties(path):
    path = Path(path)
    result = default_properties(path.stem)
    try:
        text = path.with_suffix(".cfg").read_text(encoding="utf-8")
    except FileNotFoundError:
        return result
    except (OSError, UnicodeError) as error:
        return dict(loopable=False, startup=False, is_idle=False, config_error=str(error))
    for line in text.splitlines():
        key, sep, value = line.split("#", 1)[0].partition("=")
        key, value = key.strip(), value.strip()
        if sep and key in FLAGS:
            if value in ("0", "1"):
                result[key] = value == "1"
            else:
                result[key] = False
                result["config_error"] = f"{key} must be 0 or 1 in {path.with_suffix('.cfg').name}"
    return result


def save_properties(path, values):
    if set(values) != set(FLAGS) or any(type(value) is not bool for value in values.values()):
        raise ValueError("Expected loopable, startup and is_idle boolean properties.")
    cfg = Path(path).with_suffix(".cfg")
    try:
        text = cfg.read_text(encoding="utf-8")
        mode = stat.S_IMODE(cfg.stat().st_mode)
    except FileNotFoundError:
        text, mode = "", None
    output, seen = [], set()
    for line in text.splitlines(keepends=True):
        key, sep, _value = line.split("#", 1)[0].partition("=")
        key = key.strip()
        if sep and key in FLAGS:
            if key not in seen:
                comment = " #" + line.split("#", 1)[1].rstrip("\r\n") if "#" in line else ""
                output.append(f"{key}={int(values[key])}{comment}\n")
                seen.add(key)
        else:
            output.append(line)
    if output and not output[-1].endswith(("\n", "\r")):
        output[-1] += "\n"
    output.extend(f"{key}={int(values[key])}\n" for key in FLAGS if key not in seen)
    fd, temporary = tempfile.mkstemp(prefix=cfg.name + ".", suffix=".tmp", dir=cfg.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as stream:
            if mode is not None:
                os.fchmod(stream.fileno(), mode)
            stream.write("".join(output))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, cfg)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
