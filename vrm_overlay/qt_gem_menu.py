"""Menu action registry and avatar theme owner, shared by Qt and native popups."""
from collections import OrderedDict
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import threading

from PySide6.QtCore import QObject, Property, QTimer, Signal, Slot

from .menu_design import CENTER, HEIGHT, ICONS, MAX_ACTIONS, MAX_PAGES, PAGE_SIZE, PRESETS, WIDTH, avatar_palette, layout, palette


@dataclass
class MenuAction:
    id: str
    label: str
    icon: str
    callback: object = None
    enabled: object = True
    checked: object = False
    target: str = ''
    close: bool = True
    hero: bool = False
    danger: bool = False
    group: str = 'main'
    object_name: str = ''


class GemMenu(QObject):
    changed = Signal()
    pageChanged = Signal()
    _coloursReady = Signal(str, object)

    def __init__(self, runtime):
        super().__init__(runtime)
        self.runtime=runtime
        self.actions=OrderedDict()
        self._spec={}
        self._page=0
        self._context=''
        self._model=''
        self._palette=palette()
        self._cache=OrderedDict()
        self._worker_busy=False
        self._pending_palette=None
        self._closing=False
        self._last_sent=''
        self._last_signature=None
        self._sync=QTimer(self);self._sync.setSingleShot(True);self._sync.timeout.connect(self._send_native)
        self._coloursReady.connect(self._palette_ready)
        r=runtime
        self.register(MenuAction('chat','Talk to me','chat',r.chat.openNearCursor,
                      enabled=lambda:not r.externalAgent,hero=True,object_name='overlayChatButton'))
        self.register(MenuAction('studio','Studio','studio',r.showStudio,object_name='overlayStudioButton'))
        self.register(MenuAction('pin','Always on top','pin',lambda:r.command('set_render',dict(always_on_top=not r.state['always_on_top'])),
                      enabled=lambda:r.connected,checked=lambda:r.state['always_on_top'],object_name='overlayAlwaysOnTopCheck'))
        self.register(MenuAction('visibility','Hide avatar','hide',r.toggleAvatar,enabled=lambda:r.connected,
                      object_name='overlayVisibilityButton'))
        self.register(MenuAction('colours','Colours','colours',target='themes',close=False))
        self.register(MenuAction('quit','Quit','power',r.quitApplication,danger=True,object_name='overlayQuitButton'))
        for identity,label,icon in [('auto','Avatar','avatar')]+[(k,v[0].split()[0],'spark') for k,v in PRESETS.items()]:
            self.register(MenuAction('theme:'+identity,label,icon,lambda identity=identity:self.setTheme(identity),
                          checked=lambda identity=identity:self._theme_choice()==identity,close=False,group='themes'))
        runtime.changed.connect(self.refresh)
        runtime.stateChanged.connect(self.refresh)
        self.refresh()

    def register(self, action):
        """Trusted application extensions add actions here; callbacks never cross IPC."""
        if (not isinstance(action,MenuAction) or not all(isinstance(v,str) for v in (action.id,action.label,action.icon,action.group,action.target)) or
                not re.fullmatch(r'[a-z][a-z0-9_.:-]{0,47}',action.id) or action.id in self.actions or
                not action.label or len(action.label)>28 or action.icon not in ICONS or
                not re.fullmatch(r'[a-z][a-z0-9_-]{0,23}',action.group) or
                (action.target and not re.fullmatch(r'[a-z][a-z0-9_-]{0,23}',action.target)) or
                any(type(v) is not bool for v in (action.close,action.hero,action.danger)) or
                (action.callback is not None and not callable(action.callback)) or len(self.actions)>=MAX_ACTIONS):
            raise ValueError('Invalid, duplicate or excessive gem menu action')
        groups={}
        for entry in (*self.actions.values(),action):groups[entry.group]=groups.get(entry.group,0)+1
        if sum((count+PAGE_SIZE-1)//PAGE_SIZE for count in groups.values())>MAX_PAGES:
            raise ValueError('Too many gem menu pages')
        self.actions[action.id]=action
        self._last_signature=None
        if self._spec:self.refresh()

    spec = Property('QVariantMap',lambda self:self._spec,notify=changed)
    pages = Property('QVariantList',lambda self:self._spec.get('pages',[]),notify=changed)
    theme = Property('QVariantMap',lambda self:self._spec.get('palette',palette()),notify=changed)
    icons = Property('QVariantMap',lambda self:ICONS,constant=True)
    currentPage = Property(int,lambda self:self._page,notify=pageChanged)

    @staticmethod
    def _value(value):
        return value() if callable(value) else value

    def _theme_key(self):
        return 'menu/themes/'+hashlib.sha256(self._model.encode()).hexdigest()

    def _theme_choice(self):
        choice=str(self.runtime._settings.value(self._theme_key(),'auto'))
        return choice if choice in ('auto',*PRESETS) else 'auto'

    @Slot(str)
    def setTheme(self, identity):
        if identity not in ('auto',*PRESETS):return
        self.runtime._settings.setValue(self._theme_key(),identity)
        self._last_signature=None
        self.refresh()

    def _request_palette(self, path, context):
        if self._closing:return
        self._pending_palette=(path,context)
        if self._worker_busy:return
        self._worker_busy=True
        self._pending_palette=None
        def work():
            try:
                colours=avatar_palette(path)
            except (OSError,ValueError,TypeError,KeyError,IndexError,OverflowError,AttributeError,RecursionError):
                colours=palette(name='Avatar · neutral')
            if not self._closing:
                try:self._coloursReady.emit(context,colours)
                except RuntimeError:pass
        threading.Thread(target=work,name='avatar-menu-colours',daemon=True).start()

    @Slot(str,object)
    def _palette_ready(self, context, colours):
        if self._closing:return
        self._worker_busy=False
        self._cache[context]=colours
        while len(self._cache)>16:self._cache.popitem(last=False)
        if context==self._context:
            self._palette=colours
            self._last_signature=None
            self.refresh()
        pending,self._pending_palette=self._pending_palette,None
        if pending and pending[1]!=context:self._request_palette(*pending)

    @Slot()
    def refresh(self):
        if self._closing:return
        r=self.runtime
        model=r.state.get('model_path','') if r.state.get('model_loaded') else ''
        try:
            st=Path(model).stat() if model else None
            fingerprint=f'{model}:{st.st_mtime_ns}:{st.st_size}' if st else ''
        except OSError:fingerprint=model
        context=hashlib.sha256(fingerprint.encode()).hexdigest()[:24]
        if context!=self._context:
            self._model,self._context=model,context
            self._palette=self._cache.get(context,palette())
            self._page=0
            self.pageChanged.emit()
            if model and context not in self._cache:self._request_palette(model,context)
        rows=[]
        for action in self.actions.values():
            label=action.label
            if action.id=='chat' and r.externalAgent:label='External agent'
            if action.id=='visibility' and not r.avatarShown:label='Summon avatar'
            rows.append(dict(id=action.id,label=label,icon=action.icon,enabled=bool(self._value(action.enabled)),
                             checked=bool(self._value(action.checked)),target=action.target,close=action.close,
                             hero=action.hero,danger=action.danger,group=action.group,objectName=action.object_name))
            if action.id.startswith('theme:') and action.id.split(':',1)[1] in ('auto',*PRESETS):
                identity=action.id.split(':',1)[1]
                rows[-1]['previewPalette']=self._palette if identity=='auto' else palette(*PRESETS[identity][1:],name=PRESETS[identity][0])
        choice=self._theme_choice()
        colours=self._palette if choice=='auto' else palette(*PRESETS[choice][1:],name=PRESETS[choice][0])
        signature=json.dumps([context,rows,colours,r.connected,r.state.get('window_backend')],sort_keys=True)
        if signature==self._last_signature:return
        self._last_signature=signature
        groups=list(dict.fromkeys(row['group'] for row in rows))
        current_id=self.pages[self._page]['id'] if self.pages and self._page<len(self.pages) else 'main:0'
        pages=[]
        for group in groups:
            entries=[row for row in rows if row['group']==group]
            for start in range(0,len(entries),PAGE_SIZE):
                pages.append(dict(id=f'{group}:{start//PAGE_SIZE}',group=group,
                                  title='Colours' if group=='themes' else 'Companion',
                                  items=list(layout(entries[start:start+PAGE_SIZE]))))
        self._spec=dict(version=1,context=context,width=WIDTH,height=HEIGHT,center=list(CENTER),
                        palette=colours,icons=ICONS,pages=pages)
        previous_page=self._page
        self._page=next((i for i,p in enumerate(pages) if p['id']==current_id),0)
        if self._page!=previous_page:self.pageChanged.emit()
        self.changed.emit()
        self._sync.start(0)

    @Slot(int)
    def setPage(self, index):
        if 0<=index<len(self.pages):
            self._page=index
            self.pageChanged.emit()

    @Slot(str)
    def navigate(self, group):
        index=next((i for i,p in enumerate(self.pages) if p['group']==group),0)
        self.setPage(index)

    @Slot(str)
    def activate(self, identity):
        action=self.actions.get(identity)
        if action is None or not self._value(action.enabled):return
        if action.target:self.navigate(action.target)
        elif action.callback:action.callback()

    def _send_native(self):
        r=self.runtime
        if self._closing or not r.connected or r.state.get('window_backend')!='wayland':return
        if r.busy:
            self._sync.start(50);return
        encoded=json.dumps(self._spec,sort_keys=True)
        if encoded==self._last_sent:return
        self._last_sent=encoded
        r._queue([dict(action='configure_menu',menu=self._spec)],'Update gem menu',
                 failed=lambda _message:setattr(self,'_last_sent',''))

    def shutdown(self):
        self._closing=True
        self._sync.stop()
