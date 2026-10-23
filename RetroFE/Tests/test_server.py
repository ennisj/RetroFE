"""Test server for the Catalog downloader.

  /file.bin          4 MB of deterministic bytes, with Range support
  /drop/file.bin     same, but the first request for it is cut off after 1 MB
  /norange/file.bin  same, ignoring Range (always 200)
  /flaky/file.bin    answers 503 twice, then serves
  /index.json        a small index
  /github/releases   a GitHub-style releases list (needs GitHub's Accept header; ETag and
                     If-None-Match; each full answer bumps the version's last number)
  /github/RetroFE-<version>-<system>.zip   the release's archive: retrofe/retrofe.exe and a layout
  /github/limited/releases   403, GitHub's rate limit reached
  anything else      404
Usage: python -I test_server.py PORT
Then run retrofe_tests with RETROFE_TEST_SERVER=http://127.0.0.1:PORT
"""
import hashlib
import io
import json
import sys
import time
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DATA = bytes((i * 7 + (i >> 8)) & 0xFF for i in range(4 * 1024 * 1024))
SHA = hashlib.sha256(DATA).hexdigest()
ETAG = '"' + SHA[:16] + '"'
state = {"dropped": False, "flaky": 0, "releases": 0}
log = []


def release_zip():
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("retrofe/retrofe.exe", "live exe")
        archive.writestr("retrofe/SDL2.dll", "live library")
        archive.writestr("layouts/Live/layout.xml", "<layout/>")
        archive.writestr("RetroFE.lnk", "shortcut")
    return buffer.getvalue()


RELEASE_ZIP = release_zip()
RELEASE_ETAG = '"releases"'


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s %s\n" % (self.path, self.headers.get("Range"), self.headers.get("If-Range")))

    def send_body(self, status, body, headers=()):
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def github(self, path):
        if path == "/github/limited/releases":
            self.send_body(403, b'{"message": "API rate limit exceeded"}',
                           [("X-RateLimit-Remaining", "0"), ("X-RateLimit-Reset", str(int(time.time()) + 600))])
            return
        if path.startswith("/github/RetroFE-") and path.endswith(".zip"):
            self.send_body(200, RELEASE_ZIP)
            return
        if path != "/github/releases":
            self.send_body(404, b"")
            return
        if self.headers.get("Accept") != "application/vnd.github+json":
            self.send_body(400, b'{"message": "Send GitHub\'s Accept header"}')
            return
        if self.headers.get("If-None-Match") == RELEASE_ETAG and state["releases"] > 0:
            self.send_body(304, b"")
            return
        state["releases"] += 1
        version = "2.0.%d" % state["releases"]
        host = "http://" + self.headers.get("Host")
        assets = [{"name": "RetroFE-%s-%s.zip" % (version, system), "state": "uploaded", "size": len(RELEASE_ZIP),
                   "digest": "sha256:" + hashlib.sha256(RELEASE_ZIP).hexdigest(),
                   "browser_download_url": "%s/github/RetroFE-%s-%s.zip" % (host, version, system)}
                  for system in ("windows-x64", "linux-x64", "macos-arm64", "macos-x64")]
        body = json.dumps([{"tag_name": "v" + version, "draft": False, "prerelease": False,
                            "body": "Live release", "assets": assets}]).encode()
        self.send_body(200, body, [("ETag", RELEASE_ETAG)])

    def do_GET(self):
        path = self.path
        if path.startswith("/github/"):
            self.github(path)
            return
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
        # If-Range: only resume the same version (this one never changes)
        if_range = self.headers.get("If-Range")
        if if_range is not None and if_range != ETAG:
            rng = None
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
        self.send_header("ETag", ETAG)
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
