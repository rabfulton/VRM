#!/usr/bin/env python3
"""Optional real renderer/voice check driven through the official MCP client."""
import argparse
import asyncio
import os
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('x11', 'wayland'), default='x11')
    parser.add_argument('--no-speech', action='store_true', help='Exercise gesture completion and cancellation without a prepared voice')
    args = parser.parse_args()
    os.environ.update(QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='basic', QT_QUICK_BACKEND='software')
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client
    from PySide6.QtCore import QProcess, QSettings
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtTest import QTest
    from vrm_overlay.animation_config import save_properties, load_properties
    from vrm_overlay.qt_companion import CompanionServer
    from vrm_overlay.qt_controller import Controller

    app = QGuiApplication([])
    with tempfile.TemporaryDirectory(prefix='domovushka-mcp-') as directory:
        root = Path(directory)
        library = root / 'animations'
        library.mkdir()
        for name in ('idle', 'idle_talking'):
            source = ROOT / f'assets/starter/animations/humanoid/{name}.vrma'
            target = library / source.name
            shutil.copyfile(source, target)
            save_properties(target, dict(load_properties(source), is_idle=name == 'idle', startup=name == 'idle', llm_enabled=True))
        exit_clip = library / 'Exit.vrma'
        shutil.copyfile(ROOT / 'assets/starter/animations/humanoid/idle_talking.vrma', exit_clip)
        save_properties(exit_clip, dict(loopable=False, startup=False, is_idle=False, is_exit=True, llm_enabled=False))
        bridge = Controller(str(root / 'renderer.sock'), model=str(ROOT / 'assets/starter/models/anime.vrm'),
                            animation_dir=str(library), renderer_settings=str(root / 'renderer.cfg'),
                            settings=QSettings(str(root / 'studio.ini'), QSettings.IniFormat), avatar_dir=root / 'avatars',
                            external_agent=True, window_backend=args.backend)
        path = str(root / 'companion.sock')
        server = CompanionServer(bridge, path)
        def wait(predicate, timeout=60):
            deadline = time.monotonic() + timeout
            while not predicate():
                if time.monotonic() > deadline:
                    raise AssertionError(f'{bridge.lastError}\n{bridge.logText}')
                QTest.qWait(10)
        try:
            bridge.connectRuntime()
            wait(lambda: bridge.connected and bridge.state['model_loaded'] and bridge.state['playing'])
            wait(lambda: bridge._motion and bridge._motion['mode'] == 'idle' and not bridge.busy)
            QTest.qWait(1500)
            initial_resizes = bridge.state['frame_timing']['window_resizes']
            QTest.qWait(1500)
            assert bridge.state['frame_timing']['window_resizes'] == initial_resizes, bridge.state['frame_timing']
            print('Steady X11 idle: no automatic window resizes during 1.5 seconds of playback.')
            errors = []
            playback_finished = threading.Event()
            playback_verified = threading.Event()
            async def scenario():
                params = StdioServerParameters(command=sys.executable,
                    args=['-m', 'vrm_overlay.mcp_server', '--companion-socket', path],
                    env=dict(os.environ, PYTHONPATH=str(ROOT)))
                async with stdio_client(params) as (read, write):
                    async with ClientSession(read, write) as client:
                        await client.initialize()
                        caps = (await client.call_tool('avatar_capabilities', {})).structuredContent
                        assert caps['external_agent'] and caps['connected'] and caps['gestures'], caps
                        gesture = next(c['id'] for c in caps['gestures'] if c['id'] == 'idle_talking')
                        tool = 'avatar_perform' if args.no_speech else 'avatar_present'
                        arguments = dict(gesture=gesture)
                        if not args.no_speech:
                            assert caps['speech_ready'], 'Prepare the default voice first, or pass --no-speech.'
                            arguments['text'] = 'Hello! Your external agent is connected.'
                        response = await client.call_tool(tool, arguments)
                        assert not response.isError, response
                        identifier = response.structuredContent['id']
                        deadline = time.monotonic() + 180
                        while True:
                            state = (await client.call_tool('avatar_status', dict(presentation_id=identifier))).structuredContent
                            if state['state'] in ('completed', 'failed', 'cancelled'):
                                assert state['state'] == 'completed', state
                                break
                            assert time.monotonic() < deadline, state
                            await asyncio.sleep(.1)
                        response = await client.call_tool('avatar_perform', dict(gesture=gesture))
                        cancelled = await client.call_tool('avatar_cancel', dict(presentation_id=response.structuredContent['id']))
                        assert cancelled.structuredContent['state'] == 'cancelled'
                        playback_finished.set()
                        while not playback_verified.is_set():
                            await asyncio.sleep(.01)
                        hidden = await client.call_tool('avatar_hide', {})
                        assert not hidden.isError, hidden
                        deadline = time.monotonic() + 60
                        while True:
                            visibility = (await client.call_tool('avatar_visibility', {})).structuredContent
                            if visibility['phase'] == 'hidden':
                                assert not visibility['visible'] and not visibility['requested_visible'], visibility
                                break
                            assert time.monotonic() < deadline, visibility
                            await asyncio.sleep(.1)
                        rejected = await client.call_tool('avatar_perform', dict(gesture=gesture))
                        assert rejected.isError
                        summoned = await client.call_tool('avatar_summon', {})
                        assert not summoned.isError and summoned.structuredContent['requested_visible'], summoned
                        deadline = time.monotonic() + 10
                        while True:
                            visibility = (await client.call_tool('avatar_visibility', {})).structuredContent
                            if visibility['visible'] and visibility['phase'] == 'visible':
                                break
                            assert time.monotonic() < deadline, visibility
                            await asyncio.sleep(.1)
                        # A fast double toggle is two serialized state changes,
                        # even while the first exit is still being prepared.
                        toggled = await client.call_tool('avatar_toggle', {})
                        assert not toggled.isError and not toggled.structuredContent['requested_visible'], toggled
                        toggled = await client.call_tool('avatar_toggle', {})
                        assert not toggled.isError and toggled.structuredContent['requested_visible'], toggled
                        deadline = time.monotonic() + 10
                        while True:
                            visibility = (await client.call_tool('avatar_visibility', {})).structuredContent
                            if visibility['visible'] and visibility['phase'] == 'visible':
                                break
                            assert time.monotonic() < deadline, visibility
                            await asyncio.sleep(.1)
            def work():
                try:
                    asyncio.run(scenario())
                except BaseException as error:
                    errors.append(error)
            thread = threading.Thread(target=work, daemon=True)
            thread.start()
            wait(lambda: playback_finished.is_set() or not thread.is_alive(), timeout=200)
            if errors:
                raise errors[0]
            replies = []
            bridge._queue([dict(action='audio_status')], 'Verify external playback', done=replies.append)
            wait(lambda: bool(replies))
            if not args.no_speech:
                assert replies[0]['data']['total_read'] > 0, replies
            # Hiding cancels audio and resets the counters; inspect playback first.
            playback_verified.set()
            wait(lambda: not thread.is_alive(), timeout=100)
            if errors:
                raise errors[0]
            bridge.quitApplication()
            wait(lambda: bridge._process.state() == QProcess.NotRunning)
            print(f'MCP {args.backend}: discovery, presentation completion, cancellation, animated hide/summon and Quit passed; speech={not args.no_speech}')
        finally:
            server.close()
            bridge.shutdown()


if __name__ == '__main__':
    main()
