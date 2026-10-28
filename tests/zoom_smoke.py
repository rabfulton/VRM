#!/usr/bin/env python3
"""Exercise real X11 wheel zoom, visible frames and viewport reuse in isolation."""
import argparse
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

os.environ.setdefault('QT_QPA_PLATFORMTHEME','basic')
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',type=Path,default=ROOT/'assets/starter/models/anime.vrm')
    args=parser.parse_args()
    if not args.model.is_file():parser.error('Avatar not found')
    if not shutil.which('xdotool'):parser.error('Install xdotool for real wheel input')
    from PySide6.QtGui import QGuiApplication,QCursor,QImage
    from PySide6.QtTest import QTest
    from vrm_overlay import IpcClient
    app=QGuiApplication([]);cursor=QCursor.pos()
    with tempfile.TemporaryDirectory(prefix='vrm-zoom-') as directory:
        root=Path(directory);socket=str(root/'runtime.sock')
        with (root/'renderer.log').open('w+') as log:
            process=subprocess.Popen([str(ROOT/'vrm-overlay'),'--socket',socket,'--settings',str(root/'settings.cfg')],cwd=root,
                                     stdout=log,stderr=subprocess.STDOUT)
            client=IpcClient(socket)
            def request(action,**values):return client.request(dict(action=action,**values),timeout_s=30)
            def image():
                return app.primaryScreen().grabWindow(wid).toImage().convertToFormat(QImage.Format_RGBA8888)
            try:
                deadline=time.monotonic()+10
                while not Path(socket).exists():
                    if process.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Renderer did not start')
                    QTest.qWait(20)
                request('load',path=str(args.model.resolve()))
                request('set_render',scale=.8,always_on_top=True)
                request('set_window',x=100,y=100)
                QTest.qWait(500)
                wid=int(subprocess.check_output(['xdotool','search','--pid',str(process.pid),
                                                '--name','^VRM Overlay$'],text=True).split()[0])
                request('reset_frame_timing')
                resizes=[];samples=0
                for phase,button in enumerate((4,5,5)):
                    if phase==2:
                        # Zooming out of a workarea-limited view must also
                        # release the fit factor without shrinking the surface.
                        request('set_render',scale=5)
                        QTest.qWait(300)
                    for tick in range(10):
                        geometry=dict(line.split('=',1) for line in subprocess.check_output(
                            ['xdotool','getwindowgeometry','--shell',str(wid)],text=True).splitlines())
                        frame=image();width=frame.width();height=frame.height()
                        alpha=bytes(frame.constBits())[3::4]
                        # Target a solid torso pixel, excluding the translucent
                        # shadow and using physical X11 coordinates at any DPI.
                        targets=[i for i,a in enumerate(alpha) if a>220 and height*.28<i//width<height*.55]
                        assert targets,'No avatar pixel to scroll over'
                        index=targets[len(targets)//2]
                        subprocess.run(['xdotool','mousemove','--sync',str(int(geometry['X'])+index%width),
                                        str(int(geometry['Y'])+index//width)],check=True)
                        QTest.qWait(10)
                        subprocess.run(['xdotool','click',str(button)],check=True)
                        for _ in range(4):
                            QTest.qWait(8);frame=image()
                            alpha=bytes(frame.constBits())[3::4]
                            assert len(alpha)-alpha.count(0)>100,'Avatar disappeared during wheel zoom'
                            samples+=1
                    state=request('get_state')['data']
                    expected=5*.9**10 if phase==2 else .8*1.1**10*(.9**10 if button==5 else 1)
                    assert math.isclose(state['scale'],expected,rel_tol=1e-5),('Lost wheel events',state['scale'],expected)
                    resizes.append(state['frame_timing']['window_resizes'])
                assert resizes[0]<10,('Zoom resized for every wheel notch',resizes)
                assert resizes[1]==resizes[0],('Zoom-out reallocated the native surface',resizes)
                frame=request('get_frame_bounds')['data']
                assert frame['fit_zoom']>.999,('Fit factor did not recover after zoom-out',frame)
                print(f'Wheel zoom passed: 30 notches, {samples} visible samples, {resizes[0]} growth resizes, no normal zoom-out resizes, capped zoom recovery.',flush=True)
            except Exception:
                log.flush();log.seek(0);print('\n'.join(log.read().splitlines()[-20:]),file=sys.stderr)
                raise
            finally:
                try:request('shutdown')
                except Exception:pass
                client.close()
                try:process.wait(4)
                except subprocess.TimeoutExpired:process.kill();process.wait()
                QCursor.setPos(cursor)
    return 0


if __name__=='__main__':raise SystemExit(main())
