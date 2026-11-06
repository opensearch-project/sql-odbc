#!/usr/bin/env python3
"""Fake OpenSearch whose second result page fails, for ut_driver_manager.

Listens on 127.0.0.1 only, on a free port that it prints on the first line
of stdout. Answers just enough of the SQL plugin API for the driver to
connect and run a query: the first page has STUB_ROWS rows and a cursor,
and the cursor request gets a proxy-style 502 with an HTML body. Exits when
its stdin closes, so it can't outlive the test that started it.
"""
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STUB_ROWS = 3

ROOT = {
    "name": "stub",
    "cluster_name": "odbc-cursor-stub",
    "version": {"distribution": "opensearch", "number": "2.19.0"},
}


def page(rows, cursor=None):
    body = {
        "schema": [{"name": "n", "type": "integer"}],
        "datarows": [[i] for i in range(rows)],
        "total": rows,
        "size": rows,
        "status": 200,
    }
    if cursor:
        body["cursor"] = cursor
    return body


class Handler(BaseHTTPRequestHandler):
    # HTTP/1.1 so "Expect: 100-continue" gets its 100 rather than making
    # curl wait a second before sending each POST body
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.send(200, ROOT)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            request = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            request = {}
        if self.path.split("?")[0].endswith("/close"):
            self.send(200, {"succeeded": True})
        elif "cursor" in request:
            html = b"<html><body><h1>502 Bad Gateway</h1></body></html>"
            self.send(502, html, "text/html")
        elif "SHOW TABLES" in request.get("query", "").upper():
            self.send(200, page(0))
        else:
            self.send(200, page(STUB_ROWS, cursor="stub-cursor-1"))


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(server.server_address[1], flush=True)
    sys.stdin.read()


if __name__ == "__main__":
    main()
