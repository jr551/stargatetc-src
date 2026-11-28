#!/usr/bin/env python3
"""Local mock for tests/host/director_http_harness.cpp (stand-in driver).

Serves the seven endpoints the harness expects on 127.0.0.1:<port> and then
runs the harness binary with the matching URLs.

  python3 director_mock_server.py <harness-binary> [port]
"""
import http.server
import socketserver
import subprocess
import sys
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _reply(self, status, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def _body(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        return self.rfile.read(n) if n > 0 else b""

    def do_POST(self):
        req = self._body()
        path = self.path.split("?", 1)[0]
        if path == "/echo":
            auth = self.headers.get("Authorization", "")
            tag = "bearer" if auth.startswith("Bearer ") else "none"
            text = req.decode("utf-8", "replace").strip()
            if text.endswith("}"):
                head = text[:-1].rstrip()
                sep = "" if head == "{" else ","
                out = '%s%s"auth":"%s"}' % (head, sep, tag)
            else:
                out = '{"echo":%s,"auth":"%s"}' % ('"' + text + '"', tag)
            self._reply(200, out)
        elif path == "/slow":
            time.sleep(10)
            self._reply(200, '{"slow":true}')
        elif path == "/malformed":
            self._reply(200, "plain prose from the mock", "text/plain")
        elif path == "/badjson":
            self._reply(200, '{"bots":[{"bot":1,"x":')
        elif path == "/error500":
            self._reply(500, "boom", "text/plain")
        elif path == "/big":
            self._reply(200, b"x" * 100000, "application/octet-stream")
        elif path == "/hang":
            # Accept the request, never answer; the client must time out.
            time.sleep(30)
            try:
                self._reply(200, "{}")
            except OSError:
                pass
        else:
            self._reply(404, "not found", "text/plain")


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    harness = argv[1]
    port = int(argv[2]) if len(argv) > 2 else 18765
    srv = Server(("127.0.0.1", port), Handler)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    base = "http://127.0.0.1:%d" % port
    urls = [
        base + "/echo",
        base + "/slow",
        base + "/malformed",
        base + "/badjson",
        base + "/error500",
        base + "/big",
        base + "/hang",
    ]
    try:
        rc = subprocess.call([harness] + urls)
    finally:
        srv.shutdown()
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
