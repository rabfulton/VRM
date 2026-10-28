#!/usr/bin/env python3
"""Optional X11 check: idle chaining, hidden Studio, real right-click and Quit."""
import argparse
import ctypes
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', type=Path, default=ROOT/'test.vrm')
    parser.add_argument('--animation-dir', type=Path, default=ROOT/'animation')
    args = parser.parse_args()
    clips = sorted(args.animation_dir.glob('*.vrma'))
    if not args.model.is_file() or len(clips) < 2:
        parser.error('Supply an avatar and at least two local VRMA clips.')
    if not all(shutil.which(tool) for tool in ('xdotool', 'xprop')):
        parser.error('This optional X11 check needs xdotool and xprop.')
    from PySide6.QtCore import QObject, QPoint, QSettings, Qt, QUrl
    from PySide6.QtGui import QGuiApplication, QCursor
    from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent
    from PySide6.QtQuick import QQuickItem
    from PySide6.QtQuickControls2 import QQuickStyle
    from PySide6.QtTest import QTest
    from vrm_overlay.qt_controller import Controller
    from vrm_overlay.animation_config import save_properties

    QQuickStyle.setStyle('Fusion')
    app = QGuiApplication([])
    app.setQuitOnLastWindowClosed(False)
    with tempfile.TemporaryDirectory(prefix='vrm-desktop-') as directory:
        root = Path(directory)
        for index, source in enumerate(clips[:2]):
            path = root/f'Idle{index}.vrma'
            path.symlink_to(source.resolve())
            save_properties(path, dict(loopable=True, startup=False, is_idle=True))
        with (root/'renderer.log').open('w+') as log:
            socket = str(root/'runtime.sock')
            process = subprocess.Popen([str(ROOT/'vrm-overlay'), '--socket', socket, '--settings', str(root/'settings.cfg')],
                                       cwd=root, stdout=log, stderr=subprocess.STDOUT)
            bridge = Controller(socket, attach=True, animation_dir=str(root),
                                settings=QSettings(str(root/'settings.ini'), QSettings.IniFormat))
            engine = QQmlApplicationEngine()
            engine.rootContext().setContextProperty('bridge', bridge)
            engine.load(QUrl.fromLocalFile(str(ROOT/'vrm_overlay/qml/Main.qml')))
            assert engine.rootObjects(), 'QML failed to load'
            window = engine.rootObjects()[0]
            menu = window.findChild(QObject, 'overlayMenu')
            component=QQmlComponent(engine)
            component.setData(b'''import QtQuick
import QtQuick.Window
Window { id: target; width: 100; height: 100; flags: Qt.FramelessWindowHint
    color: "#243246"; property int clicks: 0
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onPressed: target.clicks++ }
}''',QUrl())
            underlay=component.create()
            assert underlay, component.errors()
            quits = []
            menus=[]
            bridge.overlayMenuRequested.connect(menus.append)
            bridge.quitRequested.connect(lambda: quits.append(True))

            def wait(predicate, timeout=30):
                deadline = time.monotonic()+timeout
                while not predicate():
                    if bridge.lastError:
                        raise AssertionError(bridge.lastError)
                    if time.monotonic() > deadline:
                        raise AssertionError('Desktop operation timed out')
                    QTest.qWait(10)

            def transaction(commands):
                bridge._queue(commands, 'Desktop check')
                wait(lambda: not bridge.busy)

            def click(name):
                QTest.qWait(60)
                def find(parent):
                    if parent.objectName()==name:return parent
                    for child in parent.childItems():
                        found=find(child)
                        if found:return found
                    return None
                item = find(menu.contentItem())
                assert item, name
                point = item.mapToScene(item.boundingRect().center())
                QTest.mouseClick(menu, Qt.LeftButton, Qt.NoModifier,
                                 QPoint(round(point.x()), round(point.y())))

            def read_frame():
                result=[]
                bridge._queue([dict(action='get_frame_bounds')],'Read avatar',lambda reply:result.append(reply['data']))
                wait(lambda: bool(result) and not bridge.busy)
                return result[0]

            def shape_rectangles(wid):
                class Rect(ctypes.Structure):
                    _fields_=[('x',ctypes.c_short),('y',ctypes.c_short),('w',ctypes.c_ushort),('h',ctypes.c_ushort)]
                x11=ctypes.CDLL('libX11.so.6'); ext=ctypes.CDLL('libXext.so.6')
                x11.XOpenDisplay.argtypes=[ctypes.c_char_p]; x11.XOpenDisplay.restype=ctypes.c_void_p
                x11.XCloseDisplay.argtypes=[ctypes.c_void_p]
                x11.XFree.argtypes=[ctypes.c_void_p]
                ext.XShapeGetRectangles.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_int,ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_int)]
                ext.XShapeGetRectangles.restype=ctypes.POINTER(Rect)
                display=x11.XOpenDisplay(None)
                assert display
                count,order=ctypes.c_int(),ctypes.c_int()
                data=ext.XShapeGetRectangles(display,int(wid),2,ctypes.byref(count),ctypes.byref(order))
                result=[(r.x,r.y,r.w,r.h) for r in data[:count.value]]
                x11.XFree(data); x11.XCloseDisplay(display)
                return result

            pointer = subprocess.check_output(['xdotool','getmouselocation','--shell'],text=True)
            pointer = dict(line.split('=',1) for line in pointer.splitlines())
            try:
                wait(lambda: Path(socket).exists())
                bridge.connectRuntime()
                wait(lambda: bridge.connected)
                assert not window.isVisible(), 'Studio should start hidden'
                transaction([dict(action='load',path=str(args.model.resolve()),defer_show=True)])
                wait(lambda: bridge.state.get('presentation_pending'))
                assert read_frame()['visible_pixels']==0,'Bind pose leaked before startup clip load'
                transaction([dict(action='load_animation',path=str(clips[0].resolve()))])
                assert read_frame()['visible_pixels']==0,'Clip load revealed the avatar early'
                transaction([dict(action='blend_to',index=0,duration=0,loop=False)])
                assert read_frame()['visible_pixels']==0,'Blend setup revealed the avatar early'
                transaction([dict(action='play')])
                wait(lambda: not bridge.state['presentation_pending'])
                assert read_frame()['visible_pixels']>1000,'Startup did not reveal the animated avatar'
                print('Startup check: no figure before play, animated figure after play.',flush=True)
                bridge.loadModel(str(args.model.resolve()))
                wait(lambda: bridge._motion is not None and
                     bridge._state_revision>bridge._motion['after_revision'] and
                     bridge.state['animation_path']==bridge._motion['path'] and not bridge.busy)
                assert not bridge.state['loop'], 'A pool of loopable idles should play one pass each'
                first = bridge.state['animation_path']
                cold = bridge.state['frame_timing']['last_bounds_ms']
                transaction([dict(action='set_time', t=bridge.state['duration'])])
                wait(lambda: bridge.state['animation_path'] != first and not bridge.busy)
                second = bridge.state['animation_path']
                transaction([dict(action='set_time', t=bridge.state['duration'])])
                wait(lambda: bridge.state['animation_path'] == first and not bridge.busy)
                assert bridge.state['frame_timing']['last_bounds_ms'] == 0, 'Repeated idle must reuse bounds'
                timing = bridge.state['frame_timing']
                print(f'Idle chain: {Path(first).name} → {Path(second).name} → {Path(first).name}; cold bounds {cold:.1f} ms, cached 0 ms; cached clip load {timing["last_load_ms"]:.1f} ms',flush=True)

                wid = subprocess.check_output(['xdotool','search','--pid',str(process.pid),
                                               '--name','^VRM Overlay$'],text=True).splitlines()[0]
                hints = subprocess.check_output(['xprop','-id',wid,'_NET_WM_STATE','_NET_WM_WINDOW_TYPE'],text=True)
                assert '_NET_WM_STATE_SKIP_TASKBAR' in hints and '_NET_WM_STATE_SKIP_PAGER' in hints,hints
                assert '_NET_WM_WINDOW_TYPE_UTILITY' in hints,hints
                # Freeze a pose before targeting the compact avatar input region.
                bridge.command('pause',{})
                wait(lambda: not bridge.state['playing'] and not bridge.busy)
                bridge.command('set_render',dict(always_on_top=True))
                wait(lambda: bridge.state['always_on_top'] and not bridge.busy)
                bridge.command('set_window',dict(x=50,y=50))
                wait(lambda: not bridge.busy)
                underlay.show()
                QTest.qWait(150)
                geometry=subprocess.check_output(['xdotool','getwindowgeometry','--shell',wid],text=True)
                geometry=dict(line.split('=',1) for line in geometry.splitlines())
                uid=str(int(underlay.winId()))
                subprocess.run(['xdotool','windowmove','--sync',uid,geometry['X'],geometry['Y']],check=True)
                subprocess.run(['xdotool','windowsize','--sync',uid,geometry['WIDTH'],geometry['HEIGHT']],check=True)
                QTest.qWait(200)
                rectangles=shape_rectangles(wid)
                assert len(rectangles)>4,'Avatar must have a silhouette, not a rectangular input region'
                w,h=int(geometry['WIDTH']),int(geometry['HEIGHT'])
                rows=[bytearray(w) for _ in range(h)]
                for x,y,rw,rh in rectangles:
                    for row in rows[max(0,y):min(h,y+rh)]: row[max(0,x):min(w,x+rw)]=b'\1'*max(0,min(w,x+rw)-max(0,x))
                gap=None; gap_width=0
                for y in range(5,h-5,2):
                    left,right=rows[y].find(1),rows[y].rfind(1)
                    if left<0:continue
                    for run in re.finditer(b'\0{12,}',rows[y][left:right]):
                        x=left+(run.start()+run.end())//2
                        if run.end()-run.start()>gap_width and all(not any(row[x-4:x+5]) for row in rows[y-4:y+5]):
                            gap=(x,y); gap_width=run.end()-run.start()
                assert gap,'Expected a transparent gap inside the avatar bounds'
                before=underlay.property('clicks')
                clicks=[]
                for x,y in ((2,2),gap):
                    subprocess.run(['xdotool','mousemove','--sync','--window',wid,str(x),str(y),'click','1'],check=True)
                    QTest.qWait(150)
                    clicks.append(underlay.property('clicks'))
                assert underlay.property('clicks')==before+2,('Click-through failed',before,clicks,gap,geometry,underlay.geometry())
                x,y,rw,rh=max(shape_rectangles(wid),key=lambda r:r[2]*r[3])
                subprocess.run(['xdotool','mousemove','--sync','--window',wid,str(x+rw//2),str(y+rh//2),'click','3'],check=True)
                try: wait(lambda:menu.isVisible(),timeout=5)
                except AssertionError:
                    cursor=subprocess.check_output(['xdotool','getmouselocation','--shell'],text=True)
                    raise AssertionError(('Menu not visible',bridge.state.get('overlay_menu_seq'),menus,menu.isActive(),(x,y,rw,rh),cursor))
                point=QCursor.pos()
                screen=QGuiApplication.screenAt(point).availableGeometry()
                expected_x=max(screen.left(),min(round(point.x()-menu.width()/2),screen.right()+1-menu.width()))
                expected_y=max(screen.top(),min(round(point.y()-220*menu.property('menuScale')),screen.bottom()+1-menu.height()))
                assert abs(menu.x()-expected_x)<=1 and abs(menu.y()-expected_y)<=1,(menu.position(),point)
                print('Input check: background and interior gaps pass clicks through; figure opens menu at Qt cursor coordinates.',flush=True)
                QTest.qWait(80)
                subprocess.run(['xdotool','click','3'],check=True)
                wait(lambda:not menu.isVisible())
                QTest.qWait(300)
                assert not menu.isVisible(),'Second right-click reopened the menu after cancellation'
                subprocess.run(['xdotool','mousemove','--sync','--window',wid,str(x+rw//2),str(y+rh//2),'click','3'],check=True)
                wait(lambda:menu.isVisible())
                QTest.qWait(80)
                outside_clicks=underlay.property('clicks')
                subprocess.run(['xdotool','mousemove','--sync','--window',wid,'2','2','click','1'],check=True)
                wait(lambda:not menu.isVisible())
                assert underlay.property('clicks')==outside_clicks,'Menu cancellation leaked a click to the underlying window'
                print('Menu dismissal: second right-click and outside left-click close it without triggering an action.',flush=True)
                for enabled in (False,True):
                    bridge.overlayMenuRequested.emit(dict(x=100,y=100))
                    wait(lambda:menu.isVisible())
                    click('overlayAlwaysOnTopCheck')
                    wait(lambda:bridge.state['always_on_top']==enabled and not bridge.busy)
                    assert f'always_on_top={int(enabled)}\n' in (root/'settings.cfg').read_text()
                    wait(lambda:('_NET_WM_STATE_ABOVE' in subprocess.check_output(['xprop','-id',wid,'_NET_WM_STATE'],text=True))==enabled)
                bridge.overlayMenuRequested.emit(dict(x=100,y=100))
                wait(lambda:menu.isVisible())
                click('overlayStudioButton')
                wait(lambda: window.isVisible() and not menu.isVisible())
                window.close()
                wait(lambda: not window.isVisible())
                assert process.poll() is None and bridge.connected
                bridge.overlayMenuRequested.emit(dict(x=100,y=100))
                wait(lambda: menu.isVisible())
                click('overlayChatButton')
                chat = window.findChild(QObject,'chatWindow')
                wait(lambda: chat.isVisible())
                chat.close()
                bridge.overlayMenuRequested.emit(dict(x=100,y=100))
                wait(lambda: menu.isVisible())
                click('overlayQuitButton')
                wait(lambda: bool(quits) and process.poll() is not None)
                assert process.returncode == 0,process.returncode
                print('Desktop check passed: WM hints, hidden/closable Studio, actual overlay right-click, Chat, Studio and Quit.',flush=True)
                from vrm_overlay import IpcClient
                for number,expected in enumerate((True,False)):
                    restored_socket=str(root/f'restored-{number}.sock')
                    process=subprocess.Popen([str(ROOT/'vrm-overlay'),'--socket',restored_socket,'--settings',str(root/'settings.cfg')],cwd=root,stdout=log,stderr=subprocess.STDOUT)
                    wait(lambda:Path(restored_socket).exists())
                    with IpcClient(restored_socket) as restored:
                        assert restored.request(dict(action='get_state'),timeout_s=30)['data']['always_on_top']==expected
                        restored_wid=subprocess.check_output(['xdotool','search','--pid',str(process.pid),'--name','^VRM Overlay$'],text=True).splitlines()[0]
                        wait(lambda:('_NET_WM_STATE_ABOVE' in subprocess.check_output(['xprop','-id',restored_wid,'_NET_WM_STATE'],text=True))==expected)
                        if expected:
                            restored.request(dict(action='set_render',always_on_top=False))
                        restored.request(dict(action='shutdown'))
                    process.wait(timeout=5)
                    assert process.returncode==0
                print('Always on top: window-manager ABOVE hint and saved on/off choices survive renderer restarts.',flush=True)
            except Exception:
                log.flush(); log.seek(0)
                print('\n'.join(log.read().splitlines()[-20:]),file=sys.stderr)
                raise
            finally:
                bridge.shutdown()
                underlay.close()
                engine.deleteLater()
                app.processEvents()
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(4)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()
                subprocess.run(['xdotool','mousemove',pointer['X'],pointer['Y']],check=False)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
