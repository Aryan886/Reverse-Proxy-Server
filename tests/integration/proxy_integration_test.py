"""Run the real Linux executable against controlled local backends."""
import argparse
import http.client
import json
import logging
import pathlib
import signal
import socket
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
import unittest


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


class RawBackend(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(2)
        message = b""
        while b"\r\n\r\n" not in message:
            chunk = self.request.recv(4096)
            if not chunk:
                return
            message += chunk
        target = message.split(b" ")[1]
        responses = {
            b"/api/close": b"HTTP/1.0 200 OK\r\n\r\nclosed body",
            b"/api/incomplete": b"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nx",
            b"/api/chunked": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\n\r\n",
            b"/api/informational": b"HTTP/1.1 100 Continue\r\n\r\n",
            b"/api/oversized": b"HTTP/1.1 200 OK\r\nContent-Length: 1048577\r\n\r\n",
            b"/api/cookies": b"HTTP/1.1 200 OK\r\nContent-Length: 1\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\nConnection: X-Remove\r\nX-Remove: secret\r\n\r\nx",
            b"/api/bodyless": b"HTTP/1.1 204 No Content\r\n\r\n",
        }
        if target == b"/api/trickle":
            try:
                for byte in b"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx":
                    self.request.sendall(bytes([byte]))
                    time.sleep(0.05)
            except OSError as error:
                logging.debug("Expected trickle backend disconnect after proxy deadline: %s", error)
        else:
            self.request.sendall(responses.get(target, b"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"))


class RawServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class ProxyIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="student-proxy-test-")
        cls.processes = []
        cls.files = []
        cls.api_ports = [free_port(), free_port()]
        cls.static_port = free_port()
        try:
            cls.backends = [cls.start_backend(cls.api_ports[0], "api-1"),
                            cls.start_backend(cls.api_ports[1], "api-2"),
                            cls.start_backend(cls.static_port, "static-1")]
            cls.proxy, cls.port, cls.proxy_log = cls.start_proxy(cls.api_ports)
            cls.raw_server = RawServer(("127.0.0.1", 0), RawBackend)
            cls.raw_thread = threading.Thread(target=cls.raw_server.serve_forever, daemon=True)
            cls.raw_thread.start()
            cls.raw_proxy, cls.raw_port, cls.raw_log = cls.start_proxy([cls.raw_server.server_address[1]])
        except Exception:
            cls.tearDownClass()
            raise

    @classmethod
    def start_process(cls, command, port, name):
        path = pathlib.Path(cls.directory.name) / (name + ".log")
        output = path.open("w")
        cls.files.append(output)
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        cls.processes.append(process)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"{name} failed: {path.read_text()}")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    return process, path
            except OSError:
                time.sleep(0.02)
        raise RuntimeError(f"{name} did not start: {path.read_text()}")

    @classmethod
    def start_backend(cls, port, name):
        process, _ = cls.start_process([sys.executable, str(options.backend), "--port", str(port), "--id", name], port, name)
        return process

    @classmethod
    def start_proxy(cls, api_ports, policy="round-robin"):
        port = free_port()
        command = [str(options.proxy), "--port", str(port), "--static", f"127.0.0.1:{cls.static_port}",
                   "--policy", policy, "--io-timeout-ms", "750", "--connect-timeout-ms", "200"]
        for backend_port in api_ports:
            command += ["--api", f"127.0.0.1:{backend_port}"]
        process, path = cls.start_process(command, port, "proxy-" + str(port))
        return process, port, path

    @staticmethod
    def stop_process(process):
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise AssertionError("Process exceeded shutdown deadline")

    @classmethod
    def tearDownClass(cls):
        try:
            for process in reversed(getattr(cls, "processes", [])):
                cls.stop_process(process)
            if hasattr(cls, "raw_server"):
                cls.raw_server.shutdown()
                cls.raw_server.server_close()
                cls.raw_thread.join(timeout=2)
        finally:
            for output in getattr(cls, "files", []):
                output.close()
            if hasattr(cls, "directory"):
                cls.directory.cleanup()

    def exchange(self, message, port=None, fragments=False):
        with socket.create_connection(("127.0.0.1", port or self.port), timeout=3) as client:
            if fragments:
                for position in range(0, len(message), 3):
                    client.sendall(message[position:position + 3])
                    time.sleep(0.001)
            else:
                client.sendall(message)
            response = http.client.HTTPResponse(client)
            response.begin()
            body = response.read()
            headers = response.getheaders()
            self.assertEqual(response.getheader("Connection"), "close")
            if response.status not in (204, 304):
                self.assertEqual(int(response.getheader("Content-Length")), len(body))
            return response.status, body, headers

    def get(self, target, port=None):
        return self.exchange(f"GET {target} HTTP/1.1\r\nHost: demo.local\r\n\r\n".encode(), port)

    def test_round_robin_static_pool_and_segment_boundary(self):
        ids = [self.get("/api/id")[1] for _ in range(4)]
        self.assertEqual(set(ids), {b"api-1\n", b"api-2\n"})
        self.assertEqual(ids[0], ids[2])
        self.assertNotEqual(ids[0], ids[1])
        self.assertEqual(self.get("/static/id")[:2], (200, b"static-1\n"))
        self.assertEqual(self.get("/static/item")[:2], (200, b"Shared static demo content\n"))
        for path in ("/apix", "/", "/API/id", "/api%2Fid"):
            self.assertEqual(self.get(path)[0], 404)
        print("Observed API alternation:", [value.decode().strip() for value in ids])

    def test_fragmented_binary_post_and_forwarding_headers(self):
        message = b"POST /api/echo?q=1 HTTP/1.1\r\nHost: demo.local\r\nContent-Length: 6\r\n\r\na\x00b\r\nc"
        self.assertEqual(self.exchange(message, fragments=True)[:2], (200, b"a\x00b\r\nc"))
        status, body, _ = self.exchange(b"GET /api/headers?q=1 HTTP/1.1\r\nHost: demo.local\r\n"
            b"X-Forwarded-For: fake\r\nX-Real-IP: fake\r\nConnection: X-Remove\r\nX-Remove: secret\r\n\r\n")
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertEqual(data, {"target": "/api/headers?q=1", "host": "demo.local",
            "forwarded_for": "127.0.0.1", "real_ip": "127.0.0.1", "removed_header": None})

    def test_invalid_requests_and_input_limits(self):
        for headers, status in [
            (b"Host: x\r\nContent-Length: 0\r\nContent-Length: 0\r\n", 400),
            (b"Host: x\r\nTransfer-Encoding: chunked\r\n", 501),
            (b"Host: x\r\nExpect: 100-continue\r\n", 417),
            (b"Host: x\r\nContent-Length: 1048577\r\n", 413),
            (b"Host: x\r\nX-Large: " + b"x" * 20000 + b"\r\n", 431),
        ]:
            self.assertEqual(self.exchange(b"POST /api/echo HTTP/1.1\r\n" + headers + b"\r\n")[0], status)
        with socket.create_connection(("127.0.0.1", self.port), timeout=3) as client:
            client.sendall(b"POST /api/echo HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\na")
            client.shutdown(socket.SHUT_WR)
            response = http.client.HTTPResponse(client)
            response.begin()
            self.assertEqual(response.status, 400)
            response.read()

    def test_response_validation_and_repeated_cookies(self):
        self.assertEqual(self.get("/api/close", self.raw_port)[:2], (200, b"closed body"))
        self.assertEqual(self.get("/api/bodyless", self.raw_port)[:2], (204, b""))
        for target in ("incomplete", "chunked", "informational", "oversized"):
            self.assertEqual(self.get("/api/" + target, self.raw_port)[0], 502)
        status, body, headers = self.get("/api/cookies", self.raw_port)
        self.assertEqual((status, body), (200, b"x"))
        self.assertEqual([value for name, value in headers if name.lower() == "set-cookie"], ["a=1", "b=2"])
        self.assertFalse(any(name.lower() == "x-remove" for name, _ in headers))

    def test_client_and_upstream_stage_deadlines(self):
        with socket.create_connection(("127.0.0.1", self.port), timeout=3) as client:
            client.sendall(b"GET /api/id HTTP/1.1\r\nHost: ")
            started = time.monotonic()
            response = http.client.HTTPResponse(client)
            response.begin()
            self.assertEqual(response.status, 408)
            response.read()
            self.assertLess(time.monotonic() - started, 2)
        started = time.monotonic()
        self.assertEqual(self.get("/api/slow?seconds=2")[0], 504)
        self.assertLess(time.monotonic() - started, 2)
        started = time.monotonic()
        self.assertEqual(self.get("/api/trickle", self.raw_port)[0], 504)
        self.assertLess(time.monotonic() - started, 2)

    def test_dead_backend_fails_promptly_and_reservations_return_to_zero(self):
        self.stop_process(self.backends[1])
        try:
            started = time.monotonic()
            statuses = [self.get("/api/id")[0] for _ in range(4)]
            self.assertEqual(sorted(statuses), [200, 200, 502, 502])
            self.assertLess(time.monotonic() - started, 2)
            log = self.proxy_log.read_text()
            releases = [line for line in log.splitlines() if line.startswith("Released ")]
            self.assertTrue(releases)
            self.assertTrue(all(line.endswith("active=0") for line in releases))
        finally:
            self.backends[1] = self.start_backend(self.api_ports[1], "api-2")

    def test_repeated_connections_disconnects_and_descriptor_cleanup(self):
        baseline = len(list(pathlib.Path(f"/proc/{self.proxy.pid}/fd").iterdir()))
        for _ in range(40):
            self.assertEqual(self.get("/api/id")[0], 200)
        for _ in range(10):
            with socket.create_connection(("127.0.0.1", self.port), timeout=3) as client:
                client.sendall(b"GET /api/id HTTP/1.1\r\nHost: demo.local\r\n\r\n")
        self.assertEqual(self.get("/static/id")[0], 200)
        current = len(list(pathlib.Path(f"/proc/{self.proxy.pid}/fd").iterdir()))
        self.assertLessEqual(current, baseline + 2)
        print(f"Observed proxy descriptors: baseline={baseline}, after repeated clients={current}")

    def test_least_connections_option_and_bounded_shutdown(self):
        process, port, _ = self.start_proxy([self.api_ports[0]], "least-connections")
        self.assertEqual(self.get("/api/id", port)[:2], (200, b"api-1\n"))
        with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
            client.sendall(b"GET /api")
            started = time.monotonic()
            self.stop_process(process)
            self.assertLess(time.monotonic() - started, 2)
        self.assertEqual(process.returncode, 0)
        with socket.socket() as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("127.0.0.1", port))

    def test_cli_rejects_invalid_configuration(self):
        for arguments in (["--port", "0"], ["--api", "x:bad"], ["--policy", "invalid"], ["--port"], ["--unknown", "x"]):
            result = subprocess.run([str(options.proxy)] + arguments, capture_output=True, text=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("failed", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proxy", type=pathlib.Path, required=True)
    parser.add_argument("--backend", type=pathlib.Path, required=True)
    options = parser.parse_args()
    options.proxy = options.proxy.resolve()
    options.backend = options.backend.resolve()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
