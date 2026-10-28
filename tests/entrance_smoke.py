#!/usr/bin/env python3
"""Optional X11 check: displaced entrance fits before play and keeps its window stable."""
import argparse
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from vrm_overlay.client import IpcClient
from vrm_overlay.paths import settings_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', type=Path, default=ROOT / 'test.vrm')
    parser.add_argument('--animation', type=Path, default=ROOT / 'animation/Enter_from_left.vrma')
    args = parser.parse_args()
    if not args.model.is_file() or not args.animation.is_file() or not shutil.which('xdotool'):
        parser.error('Supply the local avatar/entrance and install xdotool for this X11 check.')
    x = C.CDLL('libX11.so.6')
    x.XOpenDisplay.argtypes, x.XOpenDisplay.restype = [C.c_char_p], C.c_void_p
    x.XDefaultRootWindow.argtypes, x.XDefaultRootWindow.restype = [C.c_void_p], C.c_ulong
    x.XGetGeometry.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong), *([C.POINTER(C.c_int)] * 2),
                              *([C.POINTER(C.c_uint)] * 4)]
    x.XTranslateCoordinates.argtypes = [C.c_void_p, C.c_ulong, C.c_ulong, C.c_int, C.c_int,
                                      C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_ulong)]
    x.XCloseDisplay.argtypes = [C.c_void_p]
    display = x.XOpenDisplay(None)
    if not display:
        parser.error('An X11 display is required.')
    try:
        with tempfile.TemporaryDirectory(prefix='vrm-entrance-') as directory:
            folder = Path(directory)
            # Reproduce the user's camera/scale without changing preferences.
            cfg = folder / 'renderer.cfg'
            if settings_path().is_file():
                shutil.copyfile(settings_path(), cfg)
            with (folder / 'renderer.log').open('w+') as log:
                sock = str(folder / 'renderer.sock')
                process = subprocess.Popen([str(ROOT / 'vrm-overlay'), '--window-backend', 'x11',
                    '--socket', sock, '--settings', str(cfg)], cwd=folder, stdout=log, stderr=log)
                client = IpcClient(sock)
                def request(action, **values):
                    return client.request(dict(action=action, **values), timeout_s=60)
                def geometry(wid):
                    root, child = C.c_ulong(), C.c_ulong()
                    px, py, rx, ry = (C.c_int() for _ in range(4))
                    width, height, border, depth = (C.c_uint() for _ in range(4))
                    assert x.XGetGeometry(display, wid, C.byref(root), C.byref(px), C.byref(py),
                        C.byref(width), C.byref(height), C.byref(border), C.byref(depth))
                    assert x.XTranslateCoordinates(display, wid, x.XDefaultRootWindow(display), 0, 0,
                        C.byref(rx), C.byref(ry), C.byref(child))
                    return rx.value, ry.value, width.value, height.value
                try:
                    deadline = time.monotonic() + 20
                    while not Path(sock).exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(.02)
                    request('load', path=str(args.model.resolve()), defer_show=True)
                    request('set_anim_backend', backend='ozz')
                    request('load_animation', path=str(args.animation.resolve()))
                    request('blend_to', index=0, duration=0, loop=False)
                    request('pause')
                    wid = int(subprocess.check_output(['xdotool', 'search', '--pid', str(process.pid),
                        '--name', '^VRM Overlay$'], text=True).splitlines()[0])
                    time.sleep(.1)  # Let the WM apply the initial clip-envelope fit.
                    initial = request('get_state')['data']
                    baseline = initial['frame_timing']['window_resizes']
                    rect = geometry(wid)
                    request('play')
                    samples = 0
                    rates = []
                    deadline = time.monotonic() + max(30, initial['duration'] / max(.01, initial['speed']) + 10)
                    while True:
                        state = request('get_state')['data']
                        assert not state['presentation_pending'], state
                        assert state['frame_timing']['window_resizes'] == baseline, state['frame_timing']
                        assert geometry(wid) == rect, (state['time'], rect, geometry(wid))
                        assert state['frame_rate_limit'] == 60
                        assert 0 <= state['frame_rate'] <= 61, state['frame_rate']
                        if state['time'] > .6:
                            rates.append(state['frame_rate'])
                        samples += 1
                        if state['duration'] > 0 and state['time'] >= state['duration'] - .0001:
                            break
                        assert time.monotonic() < deadline, 'Entrance did not finish'
                        time.sleep(.01)
                    print(f'{args.model.name} / {args.animation.name}: {samples} samples across '
                          f'{state["duration"]:.2f}s; no window moves or resizes after initial fit; {rect}.')
                    if rates:
                        print(f'Presented-frame average during entrance: {min(rates):.1f}–{max(rates):.1f} FPS; cap 60.')
                except BaseException:
                    log.seek(0)
                    print(log.read()[-4000:], file=sys.stderr)
                    raise
                finally:
                    try:
                        request('shutdown')
                    except Exception:
                        pass
                    client.close()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    finally:
        x.XCloseDisplay(display)


if __name__ == '__main__':
    main()
