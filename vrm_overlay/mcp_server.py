"""Optional MCP stdio adapter for a running domovushka companion."""
from __future__ import annotations

import argparse
import asyncio
import sys

from .companion import CompanionClient, TOOLS

INSTRUCTIONS = """Use this avatar to present selected responses through local speech and character actions.
Query avatar_capabilities first. Use only the returned gesture/expression IDs.
avatar_present accepts plain spoken text, not inline animation cues or Markdown.
Acceptance returns a presentation ID; poll avatar_status for completion and use
avatar_cancel to interrupt your presentation. Use avatar_hide and avatar_summon
for visibility, and avatar_visibility
to check the lifecycle phase. Visibility tools work in either conversation mode;
hiding cancels all output. Summon explicitly before presenting to a hidden avatar.
Do not narrate secrets or raw tool
output. The companion does not supply filesystem, email, command or scheduling
tools. Its built-in chat is separate from your agent conversation."""


async def serve(path):
    from mcp import types
    from mcp.server import Server
    from mcp.server.stdio import stdio_server

    client = CompanionClient(path)
    server = Server('domovushka', version='0.1.0', instructions=INSTRUCTIONS)

    @server.list_tools()
    async def list_tools():
        return [types.Tool(**tool) for tool in TOOLS]

    @server.call_tool()
    async def call_tool(name, arguments):
        try:
            return await asyncio.to_thread(client.call, name, arguments)
        except (ValueError, ConnectionError) as error:
            return types.CallToolResult(content=[types.TextContent(type='text', text=str(error))], isError=True)

    try:
        async with stdio_server() as (read, write):
            await server.run(read, write, server.create_initialization_options())
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser(prog='domovushka-mcp', description=__doc__)
    parser.add_argument('--companion-socket', help='Private companion presentation socket; never the renderer socket')
    args = parser.parse_args()
    try:
        import mcp.server  # Verify the optional SDK and its dependencies before serving.
    except ImportError:
        parser.exit(1, 'Install the optional MCP dependencies: python-mcp on Arch, or requirements-mcp.txt in your environment.\n')
    try:
        asyncio.run(serve(args.companion_socket))
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
