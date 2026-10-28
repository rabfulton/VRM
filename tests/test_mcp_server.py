"""Wire compatibility with the official MCP client over a real stdio session."""
import asyncio
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

try:
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client
    import test_presentation as fixtures
    HAS_QT = fixtures.HAS_QT
    HAS_SDK = True
except ImportError:
    HAS_SDK = False
    HAS_QT = False

ROOT = Path(__file__).resolve().parent.parent


@unittest.skipUnless(HAS_SDK and HAS_QT, 'Install requirements-mcp.txt and PySide6 for protocol integration')
class McpProtocolTests(fixtures.PresentationFixture if HAS_SDK and HAS_QT else unittest.TestCase):
    def test_official_client_discovers_validates_and_calls_the_server(self):
        from vrm_overlay.qt_companion import CompanionServer
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / 'companion.sock')
            server = CompanionServer(self.runtime, path)
            async def scenario():
                parameters = StdioServerParameters(command=sys.executable,
                    args=['-m', 'vrm_overlay.mcp_server', '--companion-socket', path],
                    env=dict(os.environ, PYTHONPATH=str(ROOT)))
                async with stdio_client(parameters) as (read, write):
                    async with ClientSession(read, write) as client:
                        initialized = await client.initialize()
                        self.assertEqual(initialized.serverInfo.name, 'domovushka')
                        tools = await client.list_tools()
                        self.assertEqual(len(tools.tools), 9)
                        bad = await client.call_tool('avatar_present', dict(text='Hi', command='rm'))
                        self.assertTrue(bad.isError)
                        caps = await client.call_tool('avatar_capabilities', {})
                        self.assertTrue(caps.structuredContent['external_agent'])
                        self.assertNotIn('model_path', caps.structuredContent)
                        accepted = await client.call_tool('avatar_present', dict(text='Hello from an external agent.'))
                        self.assertFalse(accepted.isError)
                        identifier = accepted.structuredContent['id']
                        state = await client.call_tool('avatar_status', dict(presentation_id=identifier))
                        self.assertIn(state.structuredContent['state'], ('queued', 'running', 'presenting'))
                        cancelled = await client.call_tool('avatar_cancel', dict(presentation_id=identifier))
                        self.assertEqual(cancelled.structuredContent['state'], 'cancelled')
            try:
                self.threaded(lambda: asyncio.run(scenario()), timeout=20)
            finally:
                server.close()


class McpLauncherTests(unittest.TestCase):
    def test_help_is_available_without_importing_the_optional_sdk(self):
        result = subprocess.run([sys.executable, '-m', 'vrm_overlay.mcp_server', '--help'],
                                cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--companion-socket', result.stdout)
