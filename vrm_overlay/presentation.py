"""External-agent presentations on the controller's existing speech/IPC owner."""
from __future__ import annotations

from collections import OrderedDict, deque
from pathlib import Path
import shutil
import time
import uuid

from PySide6.QtCore import QObject, QTimer

from .chat import capabilities
from .tts_install import find_crispasr, model_directory
from .tts_models import MODEL_FILES

TERMINAL = {'completed', 'cancelled', 'failed'}


def speech_configuration(config):
    """Resolve prepared local files without enabling the engine's auto-download."""
    config = config.copy()
    if config['provider'] == 'piper':
        ready = bool(shutil.which(config['executable'] or 'piper') and Path(config['model_path']).is_file()
                     and Path(config['model_path'] + '.json').is_file())
    else:
        try:
            find_crispasr(config['executable'], config['device'])
        except RuntimeError:
            return None
        assets = MODEL_FILES.get(config['model'])
        if assets:
            for key, asset in zip(('model_path', 'codec_path'), assets):
                if not config[key]:
                    path = model_directory() / asset.name
                    if not path.is_file() or path.stat().st_size != asset.size:
                        return None
                    config[key] = str(path)
        ready = bool(config['model_path'] and Path(config['model_path']).is_file()
                     and (not config['codec_path'] or Path(config['codec_path']).is_file()))
    return config if ready else None


class PresentationQueue(QObject):
    def __init__(self, runtime):
        super().__init__(runtime)
        self.runtime = runtime
        self.jobs = OrderedDict()
        self.pending = deque()
        self.active = None
        self.timer = QTimer(self)
        self.timer.setInterval(50)
        self.timer.timeout.connect(self.pump)

    def catalogue(self):
        return capabilities(self.runtime.state, self.runtime._files)

    def capabilities(self):
        available = self.catalogue()
        gestures = [dict(id=c.name, description=c.description) for c in available.values() if c.kind == 'animation']
        return dict(external_agent=self.runtime.externalAgent, connected=self.runtime.connected,
                    visibility=self.runtime.visibility.status() if hasattr(self.runtime, 'visibility') else dict(visible=True),
                    model_loaded=self.runtime.state['model_loaded'], speech_ready=bool(speech_configuration(self.runtime.speech.config)),
                    walking=dict(available=self.runtime.walking.available, busy=self.runtime.walking.busy,
                                 directions=['left', 'right'], modes=['relative','absolute'],
                                 distance_units='screen', min_distance=24, max_distance=1600,
                                 absolute_x_min=-131072, absolute_x_max=131072, anchor='viewport_center',
                                 x=(self.runtime.state['window']['x']+self.runtime.state['window']['width']/2) if self.runtime.state.get('window') else None,
                                 workarea=self.runtime.state.get('workarea')) if hasattr(self.runtime, 'walking') else dict(available=False),
                    gestures=gestures[:256], gestures_truncated=len(gestures) > 256,
                    expressions=[c.name for c in available.values() if c.kind == 'expression'],
                    max_text_characters=4096, queue_limit=16)

    @staticmethod
    def public(job):
        return {key: job[key] for key in ('id', 'state', 'error', 'created_at')}

    def status(self, owner, identifier):
        job = self.jobs.get(identifier)
        if job is None or job['owner'] != owner:
            raise ValueError('Presentation not found in this agent session.')
        return self.public(job)

    def submit(self, owner, text='', gesture='', expression=''):
        if hasattr(self.runtime, 'walking') and self.runtime.walking.busy:
            raise ValueError('Wait for walking to finish or cancel the motion before presenting.')
        if hasattr(self.runtime, 'visibility') and not self.runtime.visibility.wanted:
            raise ValueError('Summon the avatar before requesting a presentation.')
        if not self.runtime.externalAgent:
            raise ValueError('Select External agent as the conversation source in Studio.')
        if not self.runtime.connected or not self.runtime.state['model_loaded']:
            raise ValueError('Connect and load an avatar in Studio first.')
        if not all(isinstance(value, str) for value in (text, gesture, expression)) or len(text) > 4096 or '\0' in text:
            raise ValueError('Use plain text of at most 4096 characters and catalogue action IDs.')
        text = text.strip()
        if not (text or gesture or expression):
            raise ValueError('Provide text, a gesture or an expression.')
        available = self.catalogue()
        for kind, name in [('animation', gesture), ('expression', expression)]:
            if name and (kind, name) not in available:
                raise ValueError(f'Unavailable {kind}; query avatar_capabilities for permitted IDs.')
        if text and not speech_configuration(self.runtime.speech.config):
            raise ValueError('Prepare the selected voice in Studio before requesting speech.')
        if len(self.pending) + bool(self.active) >= 16:
            raise ValueError('Presentation queue is full; wait or cancel your pending work.')
        while len(self.jobs) >= 128:
            old = next((key for key, value in self.jobs.items() if value['state'] in TERMINAL), None)
            if old is None:
                raise ValueError('Too many presentations.')
            del self.jobs[old]
        identifier = uuid.uuid4().hex
        job = dict(id=identifier, owner=owner, text=text, gesture=gesture, expression=expression,
                   state='queued', error='', created_at=time.time(), context=self.context(),
                   profile=self.runtime.speech.config.copy(), actions=None, speech_generation=None,
                   expression_name=None, gesture_path=None, gesture_revision=None, motion_generation=None,
                   deadline=time.monotonic() + 1200, expression_until=0)
        self.jobs[identifier] = job
        self.pending.append(identifier)
        self.timer.start()
        return self.public(job)

    def context(self):
        return self.runtime.connected, self.runtime.state['model_path'], self.runtime.animationFolder

    def valid(self, job, available=None):
        if available is None:
            available = self.catalogue()
        return (self.runtime.externalAgent and job['context'] == self.context() and job['profile'] == self.runtime.speech.config
                and (not job['gesture'] or ('animation', job['gesture']) in available)
                and (not job['expression'] or ('expression', job['expression']) in available))

    def cancel(self, owner, identifier):
        self.status(owner, identifier)
        job = self.jobs[identifier]
        if job['state'] not in TERMINAL:
            self.finish(job, 'cancelled')
        return self.public(job)

    def cancel_owner(self, owner):
        for job in list(self.jobs.values()):
            if job['owner'] == owner and job['state'] not in TERMINAL:
                self.finish(job, 'cancelled')

    def cancel_all(self):
        for job in list(self.jobs.values()):
            if job['state'] not in TERMINAL:
                self.finish(job, 'cancelled')

    def finish(self, job, state, error=''):
        if job['state'] in TERMINAL:
            return
        job['state'], job['error'] = state, error[:512]
        try:
            self.pending.remove(job['id'])
        except ValueError:
            pass
        if job['id'] == self.active:
            # A manual preview/profile edit can already have replaced our speech.
            if job['speech_generation'] == self.runtime.speech.generation and self.runtime.speech.busy:
                self.runtime.speech.stop()
            if job['expression_name'] and self.context() == job['context'] and not self.runtime._closing:
                name = job['expression_name']
                self.runtime._queue([dict(action='set_expression', name=name, weight=0)], 'Release external expression',
                                    guard=lambda: self.runtime.connected and self.context() == job['context'])
            if state != 'completed' and job['motion_generation'] == self.runtime._motion_generation and not self.runtime._closing:
                self.runtime._invalidate_motion()
                if self.runtime.connected:
                    self.runtime.resumeIdle()
            self.active = None

    def pump(self):
        available = self.catalogue()
        for job in list(self.jobs.values()):
            if job['state'] not in TERMINAL:
                if time.monotonic() > job['deadline']:
                    self.finish(job, 'failed', 'Presentation exceeded its execution limit.')
                elif not self.valid(job, available):
                    self.finish(job, 'cancelled', 'Avatar, voice, permissions or conversation source changed.')
        if self.active is None:
            # Studio's own preview and built-in chat always retain their ownership.
            if self.runtime.speech.busy or self.runtime.chat.busy or self.runtime.busy:
                return
            while self.pending:
                job = self.jobs.get(self.pending.popleft())
                if job is not None and job['state'] == 'queued':
                    self.active = job['id']
                    job['state'] = 'running'
                    available = self.catalogue()
                    actions = []
                    if job['expression']:
                        cue = available[('expression', job['expression'])]
                        if cue.commands:
                            job['expression_name'] = cue.commands[0]['name']
                            actions.append(('expression', [dict(c) for c in cue.commands]))
                    if job['gesture']:
                        commands = [dict(c) for c in available[('animation', job['gesture'])].commands]
                        commands[1]['loop'] = False
                        job['gesture_path'] = commands[0]['path']
                        actions.append(('animation', commands))
                    job['actions'] = deque(actions)
                    self.advance(job)
                    return
            self.timer.stop()
            return
        job = self.jobs[self.active]
        speech = self.runtime.speech
        if job['speech_generation'] is not None:
            if speech.generation != job['speech_generation']:
                self.finish(job, 'failed' if speech.error else 'cancelled', speech.error or 'Speech was superseded by a local control.')
                return
            if speech.busy:
                return
            if speech.error:
                self.finish(job, 'failed', speech.error)
                return
        if job['state'] == 'running':
            if job['motion_generation'] is not None and not self.runtime.busy and self.runtime._motion is None:
                self.finish(job, 'cancelled', 'Animation was superseded by a local control.')
            return  # Await action acknowledgements.
        if time.monotonic() < job['expression_until']:
            return
        if job['gesture_path'] and job['gesture_revision'] is not None:
            state = self.runtime.state
            if self.runtime._state_revision <= job['gesture_revision']:
                return
            if state['animation_path'] == job['gesture_path'] and state['playing']:
                return
            if self.runtime._motion is None:
                self.finish(job, 'cancelled', 'Animation was superseded by a local control.')
                return
        self.finish(job, 'completed')

    def advance(self, job):
        if job['state'] in TERMINAL or not self.valid(job):
            return
        if job['actions']:
            kind, commands = job['actions'].popleft()
            def done(_reply):
                if job['state'] not in TERMINAL and self.valid(job):
                    if kind == 'animation':
                        if _reply.get('data', {}).get('already_in_pose'):
                            job['gesture_path'] = None
                        else:
                            job['gesture_revision'] = self.runtime._state_revision
                            job['motion_generation'] = self.runtime._motion_generation
                    elif not job['text'] and not job['gesture']:
                        job['expression_until'] = time.monotonic() + 4
                    self.advance(job)
            failed = lambda message: self.finish(job, 'failed', message)
            guard = lambda: job['state'] not in TERMINAL and self.valid(job)
            if kind == 'animation':
                self.runtime.queueCharacterAnimation(commands, 'External gesture', done=done, failed=failed, guard=guard)
                if job['gesture_path']:
                    job['motion_generation'] = self.runtime._motion_generation
            else:
                self.runtime._queue(commands, 'External expression', done=done, failed=failed, guard=guard)
            return
        if job['text']:
            config = speech_configuration(self.runtime.speech.config)
            if config is None:
                self.finish(job, 'failed', 'The prepared voice is no longer available.')
                return
            self.runtime.speech.speak(job['text'], config=config)
            job['speech_generation'] = self.runtime.speech.generation
            if self.runtime.speech.error:
                self.finish(job, 'failed', self.runtime.speech.error)
                return
        job['state'] = 'presenting'
