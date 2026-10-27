"""Validated local starter avatars and their matching animation libraries."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
STARTER=ROOT/'assets/starter'


def starter_avatars():
    try:catalog=json.loads((STARTER/'catalog.json').read_text())
    except (OSError,ValueError):return []
    if not isinstance(catalog,dict) or not isinstance(catalog.get('avatars'),list):return []
    rows=[]
    for entry in catalog.get('avatars',[]):
        if not isinstance(entry,dict) or not all(isinstance(entry.get(k),str) for k in ('name','model','animations','rig')):continue
        model=(STARTER/entry['model']).resolve()
        animations=(STARTER/entry['animations']).resolve()
        if model.is_relative_to(STARTER) and animations.is_relative_to(STARTER) and model.is_file() and animations.is_dir():
            rows.append(dict(name=entry['name'],path=str(model),animations=str(animations),rig=entry['rig'],camera=entry.get('camera',{})))
    return rows
