"""Small explicit pose graph shared by automatic idle and character actions."""
from collections import deque


def transition_route(entries, source, target):
    if source==target:return []
    queue=deque([(source,[])])
    visited={source}
    while queue:
        pose,path=queue.popleft()
        for entry in entries:
            start,end=entry.get('entry_pose','standing'),entry.get('exit_pose','standing')
            if entry.get('config_error') or entry.get('loopable') or start!=pose or end==start or end in visited:continue
            route=path+[entry]
            if end==target:return route
            visited.add(end);queue.append((end,route))
    return None


def current_pose(state, entries):
    entry=next((e for e in entries if e['path']==state.get('animation_path')),None)
    if entry is None:return 'standing'
    start,end=entry.get('entry_pose','standing'),entry.get('exit_pose','standing')
    if start==end:return end
    if state.get('duration',0)>0 and state.get('time',0)>=state['duration']-0.0001:return end
    return None  # Do not interrupt an unfinished pose-changing transition.
