#!/usr/bin/env python3
"""Optional real OpenGL regression for wide push poses and the mirrored variant."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
from vrm_overlay import IpcClient


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',type=Path,default=ROOT/'test.vrm')
    parser.add_argument('--animation-dir',type=Path,default=ROOT/'animation')
    args=parser.parse_args()
    clips=[args.animation_dir/f'{name}.vrma' for name in ('push_right','push_left')]
    if not args.model.is_file() or any(not path.is_file() for path in clips):
        parser.error('Supply an avatar and push_right/push_left VRMA clips (see docs/window-fitting.md).')
    with tempfile.TemporaryDirectory(prefix='vrm-push-fit-') as directory:
        log_path=Path(directory)/'renderer.log'
        with log_path.open('w+') as log:
            socket=str(Path(directory)/'runtime.sock')
            process=subprocess.Popen([str(ROOT/'vrm-overlay'),'--socket',socket],cwd=directory,stdout=log,stderr=subprocess.STDOUT)
            client=IpcClient(socket)
            def request(action,**values):
                return client.request(dict(action=action,**values),timeout_s=30)
            def check_frame():
                # GLFW's resized front buffer can be empty until the next swap.
                deadline=time.monotonic()+3
                while True:
                    time.sleep(0.08)
                    frame=request('get_frame_bounds')['data']
                    if frame['visible_pixels']>1000 or time.monotonic()>deadline:break
                assert frame['visible_pixels']>1000,frame
                assert frame['edge_pixels']==0,frame
                return frame
            try:
                for _ in range(100):
                    if Path(socket).exists():break
                    if process.poll() is not None:raise RuntimeError('Renderer exited before socket creation')
                    time.sleep(.1)
                request('load',path=str(args.model.resolve()))
                for clip in clips:
                    request('load_animation',path=str(clip.resolve()))
                    request('blend_to',index=0,duration=0,loop=False)
                    request('play')
                    request('pause')
                    duration=request('get_state')['data']['duration']
                    for index in range(21):
                        request('set_time',t=duration*index/20)
                        frame=check_frame()
                    print(f'{clip.name}: 21 poses, avatar/shadow clear of every edge; {frame["width"]}×{frame["height"]}',flush=True)
                    for angle in (1.57,3.14):
                        request('set_camera',azimuth=angle)
                        request('set_time',t=duration*.5)
                        check_frame()
                request('load_animation',path=str(clips[0].resolve()))
                request('blend_to',index=0,duration=.45,loop=False)
                request('play')
                for _ in range(8):check_frame()
                request('load_animation',path=str(clips[1].resolve()))
                request('blend_to',index=0,duration=.45,loop=False)
                request('play')
                for _ in range(8):check_frame()
                print('Push fitting passed: mirrored clips, camera orbit, transitions and live spring motion.',flush=True)
            except Exception:
                log.flush();log.seek(0)
                print('\n'.join(log.read().splitlines()[-25:]),file=sys.stderr)
                raise
            finally:
                try:request('shutdown')
                except Exception:pass
                client.close()
                try:process.wait(4)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    return 0


if __name__=='__main__':
    raise SystemExit(main())
