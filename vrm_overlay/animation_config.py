"""Behaviour flags in the same sidecar .cfg used by native animation tuning."""
from __future__ import annotations

import os
from pathlib import Path
import stat
import tempfile

FLAGS = ("loopable", "startup", "is_idle")
ACCESS_FLAGS = ('llm_enabled',)
POSES = ('standing','sitting','lying','crouching')
MOTION_KEYS = ('entry_pose','exit_pose','floor_contact')


def load_motion_properties(path):
    result=dict(entry_pose='standing',exit_pose='standing',floor_contact=True)
    try:
        text=Path(path).with_suffix('.cfg').read_text(encoding='utf-8')
    except FileNotFoundError:return result
    except (OSError,UnicodeError) as error:return dict(result,config_error=str(error))
    for line in text.splitlines():
        key,sep,value=line.split('#',1)[0].partition('=')
        key,value=key.strip(),value.strip()
        if not sep or key not in MOTION_KEYS:continue
        if key=='floor_contact' and value in ('0','1'):result[key]=value=='1'
        elif key!='floor_contact' and value in POSES:result[key]=value
        else:result['config_error']=f'Invalid {key} in {Path(path).with_suffix(".cfg").name}'
    return result


def default_properties(name):
    name = name.casefold()
    return dict(loopable=name in ("relax", "thinking"),
                startup=name == "enter_from_left", is_idle=name == "relax")


def load_access_properties(path):
    """Absent LLM access inherits the existing per-library choice/default."""
    result = {}
    try:
        text = Path(path).with_suffix('.cfg').read_text(encoding='utf-8')
    except FileNotFoundError:
        return result
    except (OSError, UnicodeError) as error:
        return dict(llm_enabled=False, config_error=str(error))
    for line in text.splitlines():
        key, sep, value = line.split('#', 1)[0].partition('=')
        key, value = key.strip(), value.strip()
        if sep and key in ACCESS_FLAGS:
            result[key] = value == '1'
            if value not in ('0', '1'):
                result['config_error'] = f'{key} must be 0 or 1 in {Path(path).with_suffix(".cfg").name}'
    return result


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
    if (not set(FLAGS).issubset(values) or set(values)-set(FLAGS)-set(MOTION_KEYS)-set(ACCESS_FLAGS) or
            any(type(values[key]) is not bool for key in (*FLAGS, *ACCESS_FLAGS) if key in values)):
        raise ValueError("Expected loopable, startup, is_idle and optional llm_enabled booleans.")
    for key in MOTION_KEYS:
        if key not in values:continue
        if (key=='floor_contact' and type(values[key]) is not bool) or (key!='floor_contact' and values[key] not in POSES):
            raise ValueError('Invalid '+key)
    def encoded(key):return str(int(values[key])) if type(values[key]) is bool else values[key]
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
        if sep and key in values:
            if key not in seen:
                comment = " #" + line.split("#", 1)[1].rstrip("\r\n") if "#" in line else ""
                output.append(f"{key}={encoded(key)}{comment}\n")
                seen.add(key)
        else:
            output.append(line)
    if output and not output[-1].endswith(("\n", "\r")):
        output[-1] += "\n"
    output.extend(f"{key}={encoded(key)}\n" for key in (*FLAGS,*ACCESS_FLAGS,*MOTION_KEYS) if key in values and key not in seen)
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
