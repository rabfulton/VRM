"""Horizontal locomotion jobs; the renderer owns frame timing and travel."""
from collections import OrderedDict
from contextlib import contextmanager
import math
from pathlib import Path
import time
import uuid

from PySide6.QtCore import QObject, QTimer

from .avatar_catalog import STARTER
from .pose_routes import current_pose

TERMINAL = {'completed', 'cancelled', 'failed'}


class WalkingController(QObject):
    def __init__(self, runtime):
        super().__init__(runtime)
        self.runtime = runtime
        self.jobs = OrderedDict()
        self.active = None
        self.owned = 0
        self.gait = str(STARTER / 'animations/humanoid/walk.vrma')
        self.fallback_idle = str(STARTER / 'animations/humanoid/idle.vrma')
        self.timer = QTimer(self)
        self.timer.setInterval(50)
        self.timer.timeout.connect(self.poll)
        runtime.stateChanged.connect(self.update)

    @contextmanager
    def own(self):
        self.owned += 1
        try:
            yield
        finally:
            self.owned -= 1

    @property
    def busy(self):
        return self.active is not None

    @property
    def available(self):
        r = self.runtime
        return (r.connected and r.state.get('humanoid', False) and r.state.get('model_loaded', False)
                and r.visibility.wanted and r.visibility.phase == 'visible')

    @staticmethod
    def public(job):
        return {k: job[k] for k in ('id', 'state', 'error', 'mode', 'direction', 'requested_distance',
                'distance', 'actual_distance', 'clamped', 'progress', 'estimated_duration_seconds',
                'remaining_seconds', 'created_at', 'start_x', 'requested_x', 'target_x', 'x')}

    def status(self, owner, identifier):
        job = self.jobs.get(identifier)
        if job is None or job['owner'] != owner:
            raise ValueError('Motion not found in this agent session.')
        return self.public(job)

    def start(self, owner, direction=None, distance=None, *, x=None, external=True):
        r = self.runtime
        absolute = x is not None
        if absolute:
            if direction is not None or distance is not None:
                raise ValueError('Provide direction and distance, or absolute x; do not mix them.')
            if type(x) not in (int, float) or not -131072 <= x <= 131072 or not math.isfinite(x):
                raise ValueError('Absolute x must be a finite screen coordinate between -131072 and 131072.')
        else:
            distance = 240 if distance is None else distance
            if direction not in ('left', 'right') or type(distance) not in (int, float) or not 24 <= distance <= 1600 or not math.isfinite(distance):
                raise ValueError('Choose left or right and a distance of 24–1600 screen units.')
        if external and not r.externalAgent:
            raise ValueError('Select External agent as the conversation source in Studio.')
        if not self.available:
            raise ValueError('Connect, load a humanoid and summon it before walking.')
        if self.busy or r.busy:
            raise ValueError('Wait for the current motion or operation to finish, or cancel it first.')
        if current_pose(r.state, r._files) != 'standing' or (r._motion and r._motion['mode'] in ('startup', 'transition', 'exit')):
            raise ValueError('Let the avatar return to a standing idle before walking.')
        window = r.state.get('window', {})
        if absolute and not window.get('width'):
            raise ValueError('Wait for a current avatar position before walking to an absolute x.')
        start_x = window.get('x', 0) + window.get('width', 0) / 2
        if absolute:
            delta = x - start_x
            distance = abs(delta)
            direction = 'left' if delta < 0 else 'right' if delta > 0 else 'none'
        else:
            delta = distance * (-1 if direction == 'left' else 1)
        if not Path(self.gait).is_file() or not Path(self.fallback_idle).is_file():
            raise ValueError('The bundled humanoid walk and idle animations are required.')
        if not r._animation_is_enabled(self.gait):
            raise ValueError('The walking animation is disabled.')
        idle = next((e for e in r._idle_entries() if e.get('entry_pose', 'standing') == e.get('exit_pose', 'standing') == 'standing'), None)
        idle_path = idle['path'] if idle else self.fallback_idle
        if not r._animation_is_enabled(idle_path):
            raise ValueError('Enable a standing idle animation before walking.')
        r.presentations.cancel_all()
        r.chat.stop()
        r.speech.stop()
        r._flush_tuning()
        r._invalidate_motion()
        while len(self.jobs) >= 128:
            self.jobs.popitem(last=False)
        # This initial estimate includes loading/turning and the final idle blend.
        # The native snapshot replaces it once the viewport and travel are known.
        cycle = 4 / 3  # Pinned starter walk; refined from native loaded duration.
        speed = max(20, r.state.get('avatar_height_pixels', 300) * .45 / cycle)
        blend = min(.45, max(0, r.state['blend']['duration']))
        estimate = distance / speed + .65 + blend
        job = dict(id='walk_' + uuid.uuid4().hex, owner=owner, state='preparing', error='',
                   mode='absolute' if absolute else 'relative', start_x=start_x, requested_x=x if absolute else start_x+delta,
                   target_x=x if absolute else start_x+delta, x=start_x,
                   direction=direction, requested_distance=distance, distance=distance, actual_distance=0,
                   clamped=False, progress=0, estimated_duration_seconds=estimate, remaining_seconds=estimate,
                   created_at=time.time(), model=r.state['model_path'], idle=idle_path, blend=blend,
                   saved_speed=max(.1, min(3, r.state['speed'])), deadline_ns=time.monotonic_ns() + int(max(180, estimate*2+30)*1e9),
                   settle_revision=None)
        self.jobs[job['id']] = self.active = job
        commands = r._animation_commands(self.gait, loop=True, duration=.2)
        commands[0]['neutral_facing'] = True
        commands[-1]['speed'] = 1
        commands.append(dict(action='start_walk', motion_id=job['id'], restore_speed=job['saved_speed'],
                             **(dict(x=x) if absolute else dict(distance=delta))))
        with self.own():
            r._queue_owned_motion(commands, f'Walk to X {x:g}' if absolute else 'Walk ' + direction, self.gait, 'locomotion',
                                  failed=lambda message: self.fail(job, message), guard=lambda: self.active is job and job['state'] == 'preparing')
        self.timer.start()
        r.changed.emit()
        return self.public(job)

    def poll(self):
        if self.active:
            self.update()
            self.runtime.refresh()

    def update(self):
        job, r = self.active, self.runtime
        if job is None:
            return
        if not r.connected or r.state['model_path'] != job['model']:
            self.finish(job, 'cancelled')
            return
        if not r._animation_is_enabled(self.gait) or not r._animation_is_enabled(job['idle']):
            self.cancel_current(restore=False)
            return
        if time.monotonic_ns() > job['deadline_ns']:
            self.fail(job, 'Motion timed out.')
            return
        native = r.state.get('walk', {})
        window = r.state.get('window', {})
        if window.get('width'):
            job['x'] = window['x'] + window['width']/2
        if job['state'] == 'settling':
            if (r._state_revision > job['settle_revision'] and r.state['animation_path'] == job['idle']
                    and r.state['playing'] and not r.state['blend']['active']):
                self.finish(job, job['terminal_state'])
            return
        if native.get('id') != job['id'] or job['state'] == 'cancelling':
            return
        for key in ('actual_distance', 'clamped', 'progress'):
            job[key] = native[key]
        for key in ('x', 'start_x', 'target_x', 'requested_x'):
            if key in native:job[key] = native[key]
        if job['mode'] == 'absolute':
            job['requested_distance'] = abs(native['requested_distance'])
            delta = native['distance']
            job['direction'] = 'left' if delta < 0 else 'right' if delta > 0 else 'none'
        job['distance'] = abs(native['distance'])
        job['estimated_duration_seconds'] = native['duration_seconds'] + job['blend']
        job['remaining_seconds'] = native['remaining_seconds'] + job['blend']
        phase = native['phase']
        if phase not in ('preparing', 'idle') and not job.get('travel_deadline_set'):
            job['travel_deadline_set'] = True
            job['deadline_ns'] = time.monotonic_ns() + int(max(30, job['remaining_seconds']*2+10)*1e9)
        if phase == 'completed':
            if abs(native['actual_distance'] - native['distance']) <= 2:
                self.restore(job)
            else:
                job['state'] = 'arriving'
                job.setdefault('arrival_deadline_ns', time.monotonic_ns() + 2_000_000_000)
                if time.monotonic_ns() > job['arrival_deadline_ns']:
                    self.fail(job, 'The desktop prevented the avatar reaching its destination.')
        elif phase == 'cancelled':
            self.cancel(job['owner'], job['id'])
        elif phase in ('preparing', 'turning', 'walking', 'turning_back'):
            job['state'] = phase

    def restore(self, job, *, cancelled=False):
        r = self.runtime
        job['state'] = 'settling'
        job['terminal_state'] = 'cancelled' if cancelled else 'completed'
        job['remaining_seconds'] = job['blend']
        job['settle_revision'] = r._state_revision
        commands = r._animation_commands(job['idle'], loop=True, duration=job['blend'])
        commands[-1]['speed'] = job['saved_speed']
        with self.own():
            r._queue_owned_motion(commands, 'Return to standing idle', job['idle'], 'idle',
                                  done=lambda _reply: job.update(settle_revision=r._state_revision),
                                  failed=lambda message: self.fail(job, message), guard=lambda: self.active is job)

    def finish(self, job, state, error=''):
        job.update(state=state, error=error, remaining_seconds=0)
        if state == 'completed':
            job['progress'] = 1
        if self.active is job:
            self.active = None
            self.timer.stop()
            self.runtime.changed.emit()

    def fail(self, job, message):
        if self.active is not job:
            return
        self.cancel(job['owner'], job['id'], restore=False)
        self.finish(job, 'failed', message)

    def cancel(self, owner, identifier, *, restore=True):
        self.status(owner, identifier)
        job, r = self.jobs[identifier], self.runtime
        if not restore:
            job['restore_on_cancel'] = False
        if job['state'] in TERMINAL or job['state'] == 'cancelling':
            return self.public(job)
        job['restore_on_cancel'] = restore
        job['state'] = 'cancelling'
        # Invalidate outstanding load/play/start commands before queuing stop.
        with self.own():
            r._invalidate_motion()
        def stopped(_reply):
            if self.active is not job:
                return
            if job['restore_on_cancel'] and r.connected and r.visibility.wanted and r.state['model_path'] == job['model']:
                self.restore(job, cancelled=True)
            else:
                self.finish(job, 'cancelled')
        if r.connected and not r._closing:
            r._queue([dict(action='cancel_walk', motion_id=job['id'])], 'Stop walking', priority=True,
                     done=stopped, failed=lambda message: self.finish(job, 'failed', message))
        else:
            self.finish(job, 'cancelled')
        return self.public(job)

    def cancel_current(self, *, restore=False):
        if self.active and not self.owned:
            self.cancel(self.active['owner'], self.active['id'], restore=restore)

    def cancel_owner(self, owner):
        if self.active and self.active['owner'] == owner:
            self.cancel_current(restore=True)

    def local(self, direction):
        try:
            self.start('menu', direction, external=False)
        except ValueError as error:
            self.runtime._fail(str(error))
