"""Local streamed chat API fixture; never contacts an AI provider."""
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading


class ChatServer:
    def __init__(self, chunks=None, *, status=200, finish=True, redirect=None, content_type="text/event-stream"):
        self.chunks = chunks if chunks is not None else ["Hello! ", "How are you?"]
        self.status, self.finish, self.redirect, self.content_type = status, finish, redirect, content_type
        self.requests = []
        fixture = self
        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"
            def log_message(self, *_args): pass
            def do_POST(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                fixture.requests.append(dict(body=json.loads(body), authorization=self.headers.get("Authorization")))
                self.send_response(fixture.status)
                self.send_header("Content-Type", fixture.content_type)
                self.send_header("Connection", "close")
                if fixture.redirect:
                    self.send_header("Location", fixture.redirect)
                self.end_headers()
                self.close_connection = True
                try:
                    if fixture.status != 200:
                        self.wfile.write(b'{"error":{"message":"test-key-must-not-leak"}}')
                        return
                    def event(text="", reason=None):
                        data = ("data: " + json.dumps(dict(choices=[dict(index=0, delta=dict(content=text), finish_reason=reason)]), ensure_ascii=False) + "\r\n\r\n").encode()
                        # Force UTF-8 and SSE frame fragmentation at the transport.
                        self.wfile.write(data[:7])
                        self.wfile.flush()
                        self.wfile.write(data[7:])
                        self.wfile.flush()
                    for value in fixture.chunks:
                        if isinstance(value, tuple):
                            text, gate = value
                            gate.wait(4)
                        else:
                            text = value
                        event(text)
                    if fixture.finish:
                        event(reason="stop")
                        self.wfile.write(b"data: [DONE]\r\n\r\n")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    pass
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.endpoint = f"http://127.0.0.1:{self.server.server_port}/v1/chat/completions"

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=1)
