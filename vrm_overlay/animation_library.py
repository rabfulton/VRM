"""Stable character cue names and conservative defaults for a local library."""
from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import unicodedata
from .animation_config import default_properties

# Preserve the original chat catalogue on the first scan of a library.
DEFAULT_ANIMATIONS = {
    "relax": ("idle", "Relaxed standing idle", True),
    "thinking": ("thinking", "Thinking pose", True),
    "vrma_02": ("greet", "Short greeting gesture", False),
    "angry": ("angry", "Act mildly annoyed", False),
}


def describe_animations(files, choices=None):
    """Enrich every row before filtering/enabling so IDs do not depend on checks.

    choices=None means first-use defaults (or a caller's explicit llm_enabled).
    A saved choice map makes new files opt-in, including newly added default clips.
    """
    rows = []
    for entry in files:
        name = entry["name"]
        filename = entry.get("file") or Path(entry["path"]).name
        default = DEFAULT_ANIMATIONS.get(name.casefold())
        slug = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode().casefold()
        slug = re.sub(r"[^a-z0-9]+", "_", slug).strip("_")
        if not slug or not slug[0].isalpha():
            slug = "animation_" + slug
        identifier = default[0] if default else slug[:48].rstrip("_")
        enabled = choices.get(filename, False) if choices is not None else entry.get("llm_enabled", default is not None)
        properties = {key: entry.get(key, value) for key, value in default_properties(name).items()}
        description = default[1] if default else "Play animation " + json.dumps(name, ensure_ascii=False)
        description += "; " + ("loopable" if properties["loopable"] else "one-shot")
        if properties["is_idle"]: description += "; idle/resting animation"
        if properties["startup"]: description += "; startup/entrance animation"
        if entry.get('entry_pose','standing')!='standing' or entry.get('exit_pose','standing')!='standing':
            description+='; '+entry.get('entry_pose','standing')+' to '+entry.get('exit_pose','standing')
        rows.append(dict(entry, **properties, file=filename, llm_id=identifier,
                         llm_enabled=enabled is True,
                         llm_description=description,
                         llm_loop=properties["loopable"]))
    counts = Counter(row["llm_id"] for row in rows)
    for row in rows:
        if counts[row["llm_id"]] > 1 or len(row["name"]) > 48:
            digest = hashlib.sha256(row["file"].encode()).hexdigest()[:10]
            row["llm_id"] += "_" + digest
    return rows
