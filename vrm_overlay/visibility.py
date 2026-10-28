"""Shared, interruptible hide/entrance lifecycle for local UI and MCP."""
import time

from PySide6.QtCore import QObject, QTimer

from .pose_routes import current_pose, transition_route


class AvatarVisibility(QObject):
    def __init__(self, runtime):
        super().__init__(runtime)
        self.runtime = runtime
        self.wanted = True
        self.phase = 'visible'
        self.generation = 0
        self.deadline = 0
        self.timer = QTimer(self)
        self.timer.setInterval(250)
        self.timer.timeout.connect(self.update)
        runtime.stateChanged.connect(self.update)

    def status(self):
        return dict(visible=bool(self.runtime.state.get('visible', True)),
                    requested_visible=self.wanted, phase=self.phase)

    def change(self, visible):
        r = self.runtime
        if not r.connected or not r.state['model_loaded']:
            raise ValueError('Connect and load an avatar first.')
        settled_mismatch = self.phase in ('visible', 'hidden') and bool(r.state.get('visible', True)) != visible
        if self.wanted == visible and self.phase != 'failed' and not settled_mismatch:
            return self.status()
        self.generation += 1
        token = self.generation
        self.wanted = visible
        r.presentations.cancel_all()
        r.chat.stop()
        r.speech.stop()
        r._invalidate_motion()
        self.phase = 'entering' if visible else 'exiting'
        self.deadline = time.monotonic() + 300
        self.timer.start()
        r.changed.emit()
        guard = lambda: token == self.generation and r.connected and not r._closing
        if visible:
            entry = next((e for e in r._files if e['enabled'] and e['startup'] and
                          not e.get('is_exit') and not e.get('config_error')), None)
            entry = entry or r._choose_idle('standing')
            # Reset the entrance while hidden. Zero blend avoids blending back
            # from the exit's final pose or showing a bind pose.
            commands = r._animation_commands(entry['path'], loop=False, duration=0) if entry else [dict(action='play')]
            commands.append(dict(action='set_visible', visible=True))
            def shown(_reply):
                if guard():
                    self.phase = 'visible'
                    r._state = dict(r._state, visible=True)
                    r.stateChanged.emit()
                    self.timer.stop()
                    r.changed.emit()
            if entry:
                r._queue_owned_motion(commands, 'Summon avatar', entry['path'], 'startup',
                                      done=shown, failed=self.failed, guard=guard)
            else:
                r._queue(commands, 'Summon avatar', done=shown,
                         failed=lambda message: self.failed(message) if guard() else None, guard=guard)
        else:
            pose = current_pose(r.state, r._files)
            entry = next((e for e in r._files if e['enabled'] and e.get('is_exit') and
                          not e.get('config_error') and pose is not None and
                          transition_route(r._files, pose, e.get('entry_pose', 'standing')) is not None), None)
            if entry:
                r._queue_pose_motion(r._animation_commands(entry['path'], loop=False),
                                     'Exit avatar', entry['path'], 'exit',
                                     failed=lambda _message: self.finish_hide(token), guard=guard)
            else:
                self.finish_hide(token)
        return self.status()

    def finish_hide(self, token):
        r = self.runtime
        if token != self.generation or self.wanted:
            return
        self.phase = 'hiding'
        r._invalidate_motion()
        def hidden(_reply):
            if token == self.generation:
                self.phase = 'hidden'
                r._state = dict(r._state, visible=False)
                r.stateChanged.emit()
                self.timer.stop()
                r.changed.emit()
        r._queue([dict(action='pause'), dict(action='set_visible', visible=False)], 'Hide avatar',
                 done=hidden, failed=lambda message: self.failed(message) if token == self.generation else None,
                 guard=lambda: token == self.generation and not self.wanted and r.connected)

    def failed(self, message):
        self.phase = 'failed'
        self.timer.stop()
        self.runtime._fail(message)

    def update(self):
        r = self.runtime
        if self.phase not in ('exiting', 'entering', 'hiding'):
            return
        if not r.connected:
            self.phase = 'failed'
            self.timer.stop()
            r.changed.emit()
            return
        if self.phase != 'exiting' or r.busy:
            return
        owner = r._motion
        if owner and r._state_revision <= owner['after_revision']:
            return
        state = r.state
        finished = (not state['loop'] and state['duration'] > 0 and state['time'] >= state['duration'] - .0001)
        # A manual action, library edit, failed route or timeout must never
        # strand the companion in an endless exit animation.
        if (time.monotonic() > self.deadline or owner is None or
                (owner['mode'] == 'exit' and finished)):
            self.finish_hide(self.generation)
