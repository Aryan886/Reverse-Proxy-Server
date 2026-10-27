"""Small local backend used by the C++ proxy demo."""
import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlsplit


class BackendHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = urlsplit(self.path).path
        if path in ("/api/id", "/static/id"):
            self.send_text(200, self.server.server_id + "\n")
        elif path == "/static/item":
            self.send_text(200, "Shared static demo content\n")
        else:
            self.send_text(404, "Backend path not found\n")

    def send_text(self, status, text):
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError) as error:
            self.log_error("Client disconnected: %s", error)
        self.close_connection = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9001)
    parser.add_argument("--id", default="api-1")
    arguments = parser.parse_args()
    if not 1 <= arguments.port <= 65535:
        parser.error("Port must be between 1 and 65535")

    with HTTPServer(("127.0.0.1", arguments.port), BackendHandler) as server:
        server.server_id = arguments.id
        print(f"Backend {arguments.id} ready on port {arguments.port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nBackend stopped")


if __name__ == "__main__":
    main()
