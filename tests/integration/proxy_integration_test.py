"""Test the classroom setup. Ports 8080 and 9001-9003 must be free."""
import argparse
import http.client
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
import unittest


class ProxyIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Detect an existing demo before starting or contacting any process.
        for port in (8080, 9001, 9002, 9003):
            with socket.socket() as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                try:
                    listener.bind(("127.0.0.1", port))
                except OSError as error:
                    raise RuntimeError(f"Stop the demo using port {port} before testing") from error

        cls.directory = tempfile.TemporaryDirectory(prefix="proxy-test-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.start_backend(9001, "api-1")
        cls.second_backend = cls.start_backend(9002, "api-2")
        cls.start_backend(9003, "static-1")
        cls.proxy = cls.start_process([str(options.proxy.resolve())], 8080, "proxy")

    @staticmethod
    def stop_process(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    @classmethod
    def start_backend(cls, port, name):
        return cls.start_process(
            [sys.executable, str(options.backend), "--port", str(port), "--id", name],
            port, name,
        )

    @classmethod
    def start_process(cls, command, port, name):
        path = pathlib.Path(cls.directory.name) / (name + ".log")
        output = path.open("w")
        cls.addClassCleanup(output.close)
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        cls.addClassCleanup(cls.stop_process, process)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"{name} failed:\n{path.read_text()}")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    return process
            except OSError:
                # Startup can take a moment; report the log if it never finishes.
                time.sleep(0.05)
        raise RuntimeError(f"{name} did not start:\n{path.read_text()}")

    def request(self, path, method="GET"):
        connection = http.client.HTTPConnection("127.0.0.1", 8080, timeout=8)
        try:
            connection.request(method, path)
            response = connection.getresponse()
            return response.status, response.read().decode()
        finally:
            connection.close()

    def test_round_robin(self):
        replies = [self.request("/api/id") for _ in range(4)]
        self.assertTrue(all(status == 200 for status, body in replies))
        names = [body.strip() for status, body in replies]
        self.assertEqual(sorted(names), ["api-1", "api-1", "api-2", "api-2"])
        self.assertNotEqual(names[0], names[1])
        self.assertEqual(names[:2], names[2:])

    def test_routing_and_missing_paths(self):
        self.assertEqual(self.request("/static/id"), (200, "static-1\n"))
        self.assertEqual(self.request("/static/item?demo=teacher"),
                         (200, "Shared static demo content\n"))
        self.assertEqual(self.request("/apix")[0], 404)
        self.assertEqual(self.request("/unknown")[0], 404)
        self.assertEqual(self.request("/api/missing"), (404, "Backend path not found\n"))

    def test_only_get_is_supported(self):
        self.assertEqual(self.request("/api/id", "POST")[0], 405)

    def test_request_can_arrive_in_parts(self):
        with socket.create_connection(("127.0.0.1", 8080), timeout=8) as client:
            client.sendall(b"GET /api")
            time.sleep(0.05)
            client.sendall(b"/id HTTP/1.1\r\nHost: localhost\r\n\r\n")
            response = http.client.HTTPResponse(client)
            response.begin()
            self.assertEqual(response.status, 200)
            self.assertIn(response.read(), (b"api-1\n", b"api-2\n"))
            response.close()

    def test_stopped_backend_does_not_stop_proxy(self):
        self.stop_process(self.second_backend)
        try:
            statuses = [self.request("/api/id")[0] for _ in range(2)]
            self.assertEqual(sorted(statuses), [200, 502])
            self.assertIsNone(self.proxy.poll())
        finally:
            self.second_backend = self.start_backend(9002, "api-2")
        self.assertEqual(self.request("/api/id")[0], 200)
        self.assertEqual(self.request("/api/id")[0], 200)

    def test_incomplete_request_times_out(self):
        with socket.create_connection(("127.0.0.1", 8080), timeout=8) as client:
            client.sendall(b"GET /api/id HTTP/1.1\r\n")
            response = http.client.HTTPResponse(client)
            response.begin()
            self.assertEqual(response.status, 400)
            self.assertEqual(response.read(), b"400 Bad Request\n")
            response.close()
        self.assertEqual(self.request("/static/id")[0], 200)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proxy", type=pathlib.Path, required=True)
    parser.add_argument("--backend", type=pathlib.Path, required=True)
    options = parser.parse_args()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
