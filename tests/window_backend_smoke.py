"""Real-display smoke check for native Wayland or X11, using isolated settings."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from vrm_overlay.client import IpcClient


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('wayland', 'x11'), required=True)
    parser.add_argument('--auto', action='store_true')
    args = parser.parse_args()
    env = dict(os.environ)
    env.pop('VRM_WINDOW_BACKEND', None)
    if args.backend == 'wayland':
        env.pop('DISPLAY', None)
    else:
        env.pop('WAYLAND_DISPLAY', None)
    with tempfile.TemporaryDirectory(prefix='vrm-backend-') as folder:
        socket = str(Path(folder) / 'renderer.sock')
        with open(Path(folder) / 'renderer.log', 'w+') as log:
            process = subprocess.Popen([
                str(ROOT / 'vrm-overlay'), '--window-backend', 'auto' if args.auto else args.backend,
                '--socket', socket, str(ROOT / 'assets/starter/models/anime.vrm'),
            ], cwd=folder, env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 20
                while not Path(socket).exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(.05)
                with IpcClient(socket) as client:
                    state = client.request({'action': 'get_state'}, timeout_s=20)['data']
                    assert state['window_backend'] == args.backend, state
                    assert state['model_loaded'], state
                    bounds = {}
                    while time.monotonic() < deadline:
                        bounds = client.request({'action': 'get_frame_bounds'})['data']
                        if bounds['visible_pixels'] > 100:
                            break
                        time.sleep(.05)
                    assert bounds['visible_pixels'] > 100, bounds
                    assert bounds['visible_pixels'] < bounds['width'] * bounds['height'], bounds
                    for visible in (False, True, False):
                        client.request({'action': 'set_visible', 'visible': visible})
                        assert client.request({'action': 'get_state'})['data']['visible'] == visible
                    client.request({'action': 'shutdown'})
                assert process.wait(timeout=10) == 0
                print(f"{args.backend}: rendered {bounds['visible_pixels']} pixels in "
                      f"{bounds['width']}x{bounds['height']}; visibility and IPC shutdown passed")
            except BaseException:
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == '__main__':
    main()
