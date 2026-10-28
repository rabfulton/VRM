"""Shared menu contract, material palettes, bounded growth and async ownership."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HAS_QT=importlib.util.find_spec('PySide6') is not None
if HAS_QT:
    os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
    os.environ.setdefault('QT_QUICK_BACKEND','software')
    os.environ['QT_QPA_PLATFORMTHEME']='basic'
    from PySide6.QtCore import QObject,QSettings,Signal,QEventLoop,QTimer
    from PySide6.QtGui import QGuiApplication,QImage,QColor
    from PySide6.QtTest import QTest
    from vrm_overlay.menu_design import ICONS,MAX_ACTIONS,PRESETS,avatar_palette,palette
    from vrm_overlay.qt_gem_menu import GemMenu,MenuAction

    class Runtime(QObject):
        changed=Signal()
        stateChanged=Signal()
        def __init__(self,directory):
            super().__init__()
            self._settings=QSettings(str(Path(directory)/'settings.ini'),QSettings.IniFormat)
            self.state=dict(model_loaded=False,model_path='',always_on_top=False,window_backend='x11')
            self.connected=self.avatarShown=True
            self.externalAgent=self.busy=False
            self.calls=[]
            self.chat=SimpleNamespace(openNearCursor=lambda:self.calls.append('chat'))
        def showStudio(self):self.calls.append('studio')
        def toggleAvatar(self):self.calls.append('visibility')
        def quitApplication(self):self.calls.append('quit')
        def command(self,*args):self.calls.append(args)


@unittest.skipUnless(HAS_QT,'PySide6 is required')
class GemMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=QGuiApplication.instance() or QGuiApplication([])

    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(prefix='gem-menu-')
        self.addCleanup(self.directory.cleanup)
        self.root=Path(self.directory.name)
        self.runtime=Runtime(self.root)
        self.menu=GemMenu(self.runtime)
        self.addCleanup(self.menu.shutdown)

    def wait(self,predicate):
        end=time.monotonic()+4
        while not predicate():
            self.assertLess(time.monotonic(),end,'Gem menu operation timed out')
            QTest.qWait(5)

    def avatar(self,name,colour):
        image=QImage(32,32,QImage.Format_RGBA8888);image.fill(QColor(colour))
        image.save(str(self.root/(name+'.png')))
        path=self.root/(name+'.gltf')
        path.write_text(json.dumps(dict(asset=dict(version='2.0'),images=[dict(uri=name+'.png')],
            textures=[dict(source=0)],materials=[dict(name='Coat',pbrMetallicRoughness=dict(baseColorTexture=dict(index=0)))])))
        return path

    def test_material_colours_drive_palette_unused_images_do_not(self):
        path=self.avatar('Blue','#2255dd')
        doc=json.loads(path.read_text())
        doc['images'].append(dict(uri='https://invalid.example/thumbnail.png'))
        doc['extensions']={'VRMC_vrm':{'meta':{'thumbnailImage':1}}}
        path.write_text(json.dumps(doc))
        result=avatar_palette(path)
        self.assertEqual(result['name'],'Avatar colours')
        self.assertGreater(int(result['primary'][5:7],16),int(result['primary'][1:3],16))
        self.assertTrue(all(len(result[k])==7 for k in ('primary','accent','face','shadow','ink')))

    def test_real_texture_palette_arrives_from_background_worker(self):
        path=self.avatar('async','#2266ee')
        loop=QEventLoop()
        def ready():
            if self.menu.theme['name']=='Avatar colours':loop.quit()
        self.menu.changed.connect(ready)
        self.runtime.state.update(model_loaded=True,model_path=str(path));self.menu.refresh()
        deadline=QTimer();deadline.setSingleShot(True);deadline.timeout.connect(loop.quit);deadline.start(4000)
        loop.exec()
        deadline.stop()
        self.assertEqual(self.menu.theme['name'],'Avatar colours')
        self.assertGreater(int(self.menu.theme['primary'][5:7],16),int(self.menu.theme['primary'][1:3],16))

    def test_neutral_and_inaccessible_textures_fall_back_without_fetching(self):
        path=self.avatar('White','#ffffff')
        self.assertIn('neutral',avatar_palette(path)['name'])
        for uri in ('../escape.png','file:///etc/passwd','https://example.invalid/picture.png'):
            doc=json.loads(path.read_text());doc['images'][0]['uri']=uri;path.write_text(json.dumps(doc))
            self.assertIn('neutral',avatar_palette(path)['name'])
        (self.root/'fake.png').write_text('<svg xmlns="http://www.w3.org/2000/svg"><image href="https://example.invalid/image.png"/></svg>')
        doc['images'][0]['uri']='fake.png';path.write_text(json.dumps(doc))
        with patch('vrm_overlay.menu_design.QImageReader') as reader:
            self.assertIn('neutral',avatar_palette(path)['name'])
            reader.assert_not_called()

    def test_material_factors_work_without_textures_and_skin_is_deprioritized(self):
        path=self.root/'factor.gltf'
        path.write_text(json.dumps(dict(materials=[
            dict(name='Skin',pbrMetallicRoughness=dict(baseColorFactor=[1,.7,.5,1])),
            dict(name='Dress',pbrMetallicRoughness=dict(baseColorFactor=[.2,.3,.9,1]))])))
        primary=avatar_palette(path)['primary']
        self.assertGreater(int(primary[5:7],16),int(primary[1:3],16))

    def test_more_actions_create_pages_and_callbacks_stay_local(self):
        called=[]
        self.menu.navigate('themes')
        for i in range(9):self.menu.register(MenuAction('extension:'+str(i),'Extra '+str(i),'spark',lambda i=i:called.append(i)))
        self.assertEqual(self.menu.pages[self.menu.currentPage]['group'],'themes')
        pages=[p for p in self.menu.pages if p['group']=='main']
        self.assertEqual([len(p['items']) for p in pages],[6,6,3])
        for p in pages:
            for row in p['items']:
                self.assertTrue(50<=row['x']<=398 and 50<=row['y']<=402)
                self.assertNotIn('callback',row)
        self.menu.activate('extension:8');self.assertEqual(called,[8])
        self.runtime.externalAgent=True;self.menu.refresh();self.menu.activate('chat')
        self.assertNotIn('chat',self.runtime.calls)
        self.menu.navigate('themes');self.assertEqual(self.menu.pages[self.menu.currentPage]['group'],'themes')

    def test_action_limit_duplicate_ids_and_icon_validation(self):
        for bad in (MenuAction('chat','Duplicate','chat'),MenuAction('bad id','Bad','spark'),MenuAction('bad','Bad','unknown')):
            with self.assertRaises(ValueError):self.menu.register(bad)
        self.menu.register(MenuAction('theme:custom','Custom action','spark'))
        while len(self.menu.actions)<MAX_ACTIONS:
            i=len(self.menu.actions)
            self.menu.register(MenuAction('extra:'+str(i),'✦'*28,'spark'))
        self.assertLess(len(json.dumps(dict(action='configure_menu',menu=self.menu.spec)).encode()),65536)
        with self.assertRaises(ValueError):self.menu.register(MenuAction('overflow','Overflow','spark'))

    def test_group_page_limit_matches_native_backend_and_rejection_is_atomic(self):
        for i in range(14):self.menu.register(MenuAction('group:'+str(i),'Tool','spark',group='group'+str(i)))
        self.assertEqual(len(self.menu.pages),16)
        with self.assertRaisesRegex(ValueError,'pages'):
            self.menu.register(MenuAction('one_more','Tool','spark',group='overflow'))
        self.assertEqual(len(self.menu.pages),16)
        self.assertNotIn('one_more',self.menu.actions)

    def test_theme_choice_is_per_avatar_and_stale_worker_cannot_recolour_new_avatar(self):
        first=self.avatar('first','#3355dd');second=self.avatar('second','#33bb66')
        gate,started=threading.Event(),threading.Event()
        self.addCleanup(gate.set)
        def extract(path):
            if str(path)==str(first):started.set();gate.wait(3);return palette('#3355dd','#f4cc88','First')
            return palette('#33bb66','#ffeedd','Second')
        with patch('vrm_overlay.qt_gem_menu.avatar_palette',side_effect=extract):
            self.runtime.state.update(model_loaded=True,model_path=str(first));self.menu.refresh()
            self.wait(started.is_set)
            self.menu.setTheme('mint')
            self.runtime.state['model_path']=str(second);self.menu.refresh();gate.set()
            self.wait(lambda:self.menu.theme['name']=='Second')
            self.runtime.state['model_path']=str(first);self.menu.refresh()
            self.assertEqual(self.menu.theme['name'],'Jade')
            self.menu.setTheme('auto');self.assertEqual(self.menu.theme['name'],'First')

    def test_native_parser_accepts_the_same_paginated_spec_and_rejects_invalid_updates(self):
        probe=Path(os.environ.get('VRM_TEST_GEM_MENU_PROBE','build/tests/gem-menu-test'))
        if not probe.is_file():self.skipTest('Native menu probe not built')
        for i in range(10):self.menu.register(MenuAction('future:'+str(i),'Future '+str(i),'spark'))
        path=self.root/'menu.json';path.write_text(json.dumps(self.menu.spec))
        result=subprocess.run([str(probe.resolve()),str(path)],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('atomic validation',result.stdout)
