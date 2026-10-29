#!/usr/bin/env python3
"""Real renderer/companion walking, completion, clamping and cancellation; no AI."""
import argparse
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=['x11', 'wayland'], default='x11')
    parser.add_argument('--model', type=Path, default=ROOT/'assets/starter/models/anime.vrm')
    parser.add_argument('--capture-prefix', type=Path, help='Optional X11 avatar-window screenshots')
    args = parser.parse_args()
    from PySide6.QtCore import QSettings
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtTest import QTest
    from vrm_overlay.animation_config import save_properties
    from vrm_overlay.companion import CompanionClient
    from vrm_overlay.qt_companion import CompanionServer
    from vrm_overlay.qt_controller import Controller

    app = QGuiApplication([])
    app.setQuitOnLastWindowClosed(False)
    with tempfile.TemporaryDirectory(prefix='domovushka-walking-') as directory:
        root = Path(directory)
        idle = root/'Idle.vrma'
        shutil.copyfile(ROOT/'assets/starter/animations/humanoid/idle.vrma', idle)
        save_properties(idle, dict(loopable=True, startup=False, is_idle=True))
        log = (root/'renderer.log').open('w+')
        process = subprocess.Popen([str(ROOT/'vrm-overlay'), '--window-backend', args.backend,
                                    '--socket', str(root/'renderer.sock'), '--settings', str(root/'renderer.cfg')],
                                   cwd=root, stdout=log, stderr=subprocess.STDOUT)
        bridge = Controller(str(root/'renderer.sock'), attach=True, animation_dir=str(root),
                            model=str(args.model.resolve()), external_agent=True,
                            settings=QSettings(str(root/'studio.ini'), QSettings.IniFormat), avatar_dir=root/'avatars')
        server = CompanionServer(bridge, root/'companion.sock')
        client = CompanionClient(str(root/'companion.sock'))
        def wait(predicate, seconds=30):
            deadline = time.monotonic()+seconds
            while not predicate():
                assert process.poll() is None, 'Renderer exited'
                assert time.monotonic()<deadline, 'Timed out: '+bridge.lastError
                QTest.qWait(10)

        def call(name, **arguments):
            results, errors = [], []
            def worker():
                try:results.append(client.call(name, arguments))
                except BaseException as error:errors.append(error)
            thread = threading.Thread(target=worker, daemon=True)
            thread.start()
            wait(lambda:not thread.is_alive())
            if errors:raise errors[0]
            return results[0]

        def native(commands):
            results = []
            bridge._queue(commands, 'Walking smoke', done=results.append)
            wait(lambda:bool(results) and not bridge.busy)
            bridge.refresh()
            wait(lambda:not bridge.busy and bridge._pending is None)
            return results[0].get('data', {})

        def center():
            native([dict(action='get_state')])
            w,a = bridge.state['window'],bridge.state['workarea']
            native([dict(action='set_window',x=a['x']+(a['width']-w['width'])//2,
                         y=a['y']+(a['height']-w['height'])//2)])

        def finish(identifier):
            records, sizes = [], set()
            captured = False
            deadline = time.monotonic()+30
            while True:
                status = call('avatar_motion_status', motion_id=identifier)
                records.append(status)
                if status['state'] in ('turning', 'walking', 'turning_back'):
                    rect = bridge.state['window']
                    sizes.add((rect['width'],rect['height']))
                    assert bridge.state['animation_neutral_facing']
                    yaw=bridge.state['walk']['yaw']
                    sign=-1 if status['direction']=='left' else 1
                    assert -.1<=sign*yaw<=90.1, ('Long or wrong-way turn',yaw,status)
                if status['state'] == 'walking' and not captured:
                    pixels=native([dict(action='get_frame_bounds')])
                    assert pixels['visible_pixels']>100 and pixels['edge_pixels']==0,pixels
                    if args.capture_prefix and args.backend=='x11':
                        wid=int(subprocess.check_output(['xdotool','search','--pid',str(process.pid),
                                                         '--name','^VRM Overlay$'],text=True).split()[0])
                        app.primaryScreen().grabWindow(wid).save(str(args.capture_prefix)+'-'+status['direction']+'.png')
                    captured=True
                if status['state'] in ('completed', 'failed', 'cancelled'):
                    break
                assert time.monotonic()<deadline, records
                QTest.qWait(50)
            assert status['state']=='completed',status
            assert len(sizes)<=1, ('Viewport resized during travel',sizes)
            assert bridge.state['animation_path']==str(idle)
            assert not bridge.state['blend']['active']
            assert abs(bridge.state['speed']-1.4)<.01
            return status,records

        try:
            bridge.connectRuntime()
            wait(lambda:bridge.connected and not bridge.busy)
            bridge._start_avatar(str(args.model.resolve()), keep_animation_folder=True)
            wait(lambda:bridge.state['model_loaded'] and bridge.state['animation_path']==str(idle) and not bridge.busy)
            native([dict(action='set_camera',azimuth=3.141592653589793)])
            native([dict(action='set_playback',speed=1.4)])
            assert call('avatar_capabilities')['walking']['available']
            for direction,sign in [('left',-1),('right',1)]:
                center()
                job=call('avatar_walk',direction=direction,distance=160)
                assert job['state']=='preparing' and job['estimated_duration_seconds']>0
                status,records=finish(job['id'])
                assert abs(status['actual_distance']-sign*160)<=2,status
                assert not status['clamped'],status
                assert any(s['state']=='walking' for s in records),records
                print(direction,status,flush=True)
                pixels=native([dict(action='get_frame_bounds')])
                assert pixels['visible_pixels']>100 and pixels['edge_pixels']==0,pixels
            center()
            for delta in (-80,120,1.5,0):
                w=bridge.state['window']
                target=w['x']+w['width']/2+delta
                job=call('avatar_walk',x=target)
                assert job['mode']=='absolute' and job['requested_x']==target,job
                status,_=finish(job['id'])
                assert abs(status['x']-target)<=2,status
                assert abs(status['target_x']-target)<=.6,status
                assert not status['clamped'],status
                print('absolute',status,flush=True)
            job=call('avatar_walk',x=-100000)
            status,_=finish(job['id'])
            assert status['clamped'] and abs(status['x']-status['target_x'])<=2,status
            assert status['requested_x']==-100000,status
            print('absolute clamped',status,flush=True)
            center()
            w,a=bridge.state['window'],bridge.state['workarea']
            native([dict(action='set_window',x=a['x']+a['width']-w['width']-80,y=w['y'])])
            job=call('avatar_walk',direction='right',distance=1600)
            status,_=finish(job['id'])
            assert status['clamped'] and status['distance']<1600,status
            print('clamped',status,flush=True)
            center()
            job=call('avatar_walk',direction='left',distance=300)
            wait(lambda:bridge.state.get('walk',{}).get('phase')=='walking')
            call('avatar_motion_cancel',motion_id=job['id'])
            wait(lambda:not bridge.walking.busy)
            assert call('avatar_motion_status',motion_id=job['id'])['state']=='cancelled'
            assert bridge.state['visible']
            center()
            job=call('avatar_walk',direction='right',distance=300)
            wait(lambda:bridge.state.get('walk',{}).get('id')==job['id'] and bridge.state['walk']['phase']=='walking')
            client.close()
            wait(lambda:not bridge.walking.busy)
            assert bridge.walking.jobs[job['id']]['state']=='cancelled'
            assert bridge.state['animation_path']==str(idle)
            print('Walking smoke passed',flush=True)
        except BaseException:
            log.flush();log.seek(0)
            print(log.read()[-16000:],file=sys.stderr)
            raise
        finally:
            client.close();server.close();bridge.shutdown()
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
            log.close()


if __name__=='__main__':main()
