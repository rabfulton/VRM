#!/usr/bin/env python3
"""Exercise every bundled CC0 motion with the real renderer and matching avatar."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import math

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
from vrm_overlay import IpcClient
from vrm_overlay.avatar_catalog import starter_avatars
from vrm_overlay.animation_config import load_motion_properties


def main():
    with tempfile.TemporaryDirectory(prefix='vrm-starter-') as directory:
        log_path=Path(directory)/'renderer.log'
        with log_path.open('w+') as log:
            socket=str(Path(directory)/'runtime.sock')
            process=subprocess.Popen([str(ROOT/'vrm-overlay'),'--socket',socket],cwd=directory,stdout=log,stderr=subprocess.STDOUT)
            client=IpcClient(socket)
            def request(action,**values):return client.request(dict(action=action,**values),timeout_s=30)
            try:
                for _ in range(100):
                    if Path(socket).exists():break
                    if process.poll() is not None:raise RuntimeError('Renderer exited')
                    time.sleep(.05)
                for avatar in starter_avatars():
                    request('load',path=avatar['path'])
                    if avatar.get('camera'):request('set_camera',**avatar['camera'])
                    request('set_anim_backend',backend='ozz')
                    files=sorted(p for p in Path(avatar['animations']).iterdir() if p.suffix in ('.vrma','.glb'))
                    assert files
                    for path in files:
                        request('load_animation',path=str(path))
                        request('blend_to',index=0,duration=0,loop=False)
                        request('play');request('pause')
                        state=request('get_state')['data']; duration=state['duration']
                        props=load_motion_properties(path)
                        assert state['foot_lock']['clip_allowed']==props['floor_contact'],path
                        if avatar['rig']=='generic':assert state['foot_lock']['supported']==0
                        else:assert state['foot_lock']['supported']==2
                        assert duration>0 and math.isfinite(duration),path
                        for fraction in (0,.33,.67,1):
                            request('set_time',t=duration*fraction)
                            time.sleep(.06)
                            frame=request('get_frame_bounds')['data']
                            assert frame['visible_pixels']>100,path
                            assert frame['edge_pixels']==0,(path,frame)
                            assert all(math.isfinite(frame[f'world_{kind}_{axis}']) for kind in ('min','max') for axis in range(3))
                        print(f'{avatar["name"]}: {path.stem}, {duration:.2f}s, floor contacts {props["floor_contact"]}',flush=True)
                    print(f'{avatar["name"]}: {len(files)} clips passed.',flush=True)
                log.flush();log.seek(0)
                output=log.read()
                assert 'Failed to load external' not in output and 'File not found' not in output,'Starter assets must embed their textures'
            except Exception:
                log.flush();log.seek(0)
                print('\n'.join(log.read().splitlines()[-20:]),file=sys.stderr)
                raise
            finally:
                try:request('shutdown')
                except Exception:pass
                client.close()
                try:process.wait(4)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    return 0


if __name__=='__main__':raise SystemExit(main())
