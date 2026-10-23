"""Test server for the Catalog downloader.

  /file.bin          4 MB of deterministic bytes, with Range support
  /drop/file.bin     same, but the first request for it is cut off after 1 MB
  /norange/file.bin  same, ignoring Range (always 200)
  /flaky/file.bin    answers 503 twice, then serves
  /index.json        a small index
  anything else      404
Usage: python -I test_server.py PORT
Then run retrofe_tests with RETROFE_TEST_SERVER=http://127.0.0.1:PORT
"""
import hashlib
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DATA = bytes((i * 7 + (i >> 8)) & 0xFF for i in range(4 * 1024 * 1024))
SHA = hashlib.sha256(DATA).hexdigest()
state = {"dropped": False, "flaky": 0}
log = []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.path, self.headers.get("Range")))

    def do_GET(self):
        path = self.path
        if path == "/index.json":
            body = json.dumps({"packages": [{"id": "big", "name": "Big", "version": "1",
                                             "files": [{"url": "file.bin", "size": len(DATA), "sha256": SHA}]}]}).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/flaky/file.bin" and state["flaky"] < 2:
            state["flaky"] += 1
            self.send_response(503)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if path not in ("/file.bin", "/drop/file.bin", "/norange/file.bin", "/flaky/file.bin"):
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        start = 0
        rng = self.headers.get("Range")
        if rng and path != "/norange/file.bin" and rng.startswith("bytes="):
            start = int(rng[6:].split("-")[0])
            if start >= len(DATA):
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % len(DATA))
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, len(DATA) - 1, len(DATA)))
        else:
            self.send_response(200)
        body = DATA[start:]
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if path == "/drop/file.bin" and not state["dropped"]:
            state["dropped"] = True
            self.wfile.write(body[:1024 * 1024])
            self.wfile.flush()
            self.connection.close()
            return
        self.wfile.write(body)


if __name__ == "__main__":
    port = int(sys.argv[1])
    print("sha256", SHA, flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
