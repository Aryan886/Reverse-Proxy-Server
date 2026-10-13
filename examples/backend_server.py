"""Controlled HTTP/1.1 backend for the student proxy demonstration."""
import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit


class BackendHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def send_body(self, status, body, content_type="text/plain; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Backend-ID", self.server.server_id)
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError) as error:
            self.log_error("client disconnected while replying: %s", error)
        self.close_connection = True

    def do_GET(self):
        target = urlsplit(self.path)
        path = target.path
        for prefix in ("/api", "/static"):
            if path == prefix or path.startswith(prefix + "/"):
                path = path[len(prefix):] or "/"
                break
        if path == "/slow":
            try:
                seconds = float(parse_qs(target.query).get("seconds", ["7"])[0])
                if not 0 <= seconds <= 10:
                    raise ValueError("delay must be between 0 and 10 seconds")
            except ValueError:
                self.send_body(400, b"invalid delay\n")
                return
            time.sleep(seconds)
            self.send_body(200, (self.server.server_id + "\n").encode())
        elif path == "/id":
            self.send_body(200, (self.server.server_id + "\n").encode())
        elif path == "/health":
            self.send_body(200, b"healthy\n")
        elif path == "/item":
            self.send_body(200, b"Shared static demo content\n")
        elif path == "/headers":
            body = json.dumps({
                "target": self.path,
                "host": self.headers.get("Host"),
                "forwarded_for": self.headers.get("X-Forwarded-For"),
                "real_ip": self.headers.get("X-Real-IP"),
                "removed_header": self.headers.get("X-Remove"),
            }).encode()
            self.send_body(200, body, "application/json")
        else:
            self.send_body(404, b"backend path not found\n")

    def do_POST(self):
        path = urlsplit(self.path).path
        if path not in ("/echo", "/api/echo", "/static/echo"):
            self.send_body(404, b"backend path not found\n")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 <= length <= 1024 * 1024:
                raise ValueError("body exceeds demo limit")
        except ValueError:
            self.send_body(400, b"invalid body length\n")
            return
        self.connection.settimeout(5)
        body = self.rfile.read(length)
        if len(body) != length:
            self.send_body(400, b"incomplete body\n")
            return
        self.send_body(200, body, "application/octet-stream")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9001)
    parser.add_argument("--id", default="api-1")
    arguments = parser.parse_args()
    if not 1 <= arguments.port <= 65535:
        parser.error("port must be between 1 and 65535")
    server = ThreadingHTTPServer(("127.0.0.1", arguments.port), BackendHandler)
    server.server_id = arguments.id
    print(f"Backend {arguments.id} listening on port {arguments.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
