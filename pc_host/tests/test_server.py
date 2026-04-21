from __future__ import annotations

import http.client
import json
import pathlib
import socket
import sys
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from znjj_host.server import HostApplication  # noqa: E402


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class HostApplicationTests(unittest.TestCase):
    def start_app(self, *, open_default_transport: bool, tcp_port: int | None = None) -> HostApplication:
        app = HostApplication(
            tcp_host="127.0.0.1",
            tcp_port=tcp_port or _free_port(),
            http_host="127.0.0.1",
            http_port=0,
        )
        app.start(open_default_transport=open_default_transport)
        self.addCleanup(app.stop)
        return app

    def request_json(
        self,
        app: HostApplication,
        method: str,
        path: str,
        payload: dict[str, object] | None = None,
    ) -> tuple[int, dict[str, object]]:
        host, port = app.http_address
        conn = http.client.HTTPConnection(host, port, timeout=2)
        body = None if payload is None else json.dumps(payload)
        headers = {} if payload is None else {"Content-Type": "application/json"}
        conn.request(method, path, body=body, headers=headers)
        response = conn.getresponse()
        data = response.read()
        conn.close()
        decoded = json.loads(data.decode("utf-8")) if data else {}
        return response.status, decoded

    def fetch_state(self, app: HostApplication) -> dict[str, object]:
        status, payload = self.request_json(app, "GET", "/api/state")
        self.assertEqual(status, 200)
        return payload

    def wait_until(self, predicate, *, timeout: float = 2.0, step: float = 0.05) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if predicate():
                return
            time.sleep(step)
        self.fail("condition not met before timeout")

    def read_first_sse_event(self, app: HostApplication) -> dict[str, object]:
        host, port = app.http_address
        conn = http.client.HTTPConnection(host, port, timeout=2)
        conn.request("GET", "/api/events")
        response = conn.getresponse()
        self.assertEqual(response.status, 200)

        lines: list[str] = []
        while True:
            line = response.readline().decode("utf-8")
            if line in {"\n", "\r\n", ""}:
                break
            lines.append(line.strip())

        conn.close()
        data_line = next(line for line in lines if line.startswith("data: "))
        return json.loads(data_line.removeprefix("data: "))

    def open_sse(self, app: HostApplication) -> tuple[http.client.HTTPConnection, http.client.HTTPResponse]:
        host, port = app.http_address
        conn = http.client.HTTPConnection(host, port, timeout=2)
        conn.request("GET", "/api/events")
        response = conn.getresponse()
        self.assertEqual(response.status, 200)
        return conn, response

    def read_sse_event(self, response: http.client.HTTPResponse) -> dict[str, object]:
        lines: list[str] = []
        while True:
            line = response.readline().decode("utf-8")
            if line in {"\n", "\r\n", ""}:
                break
            lines.append(line.strip())

        data_line = next(line for line in lines if line.startswith("data: "))
        return json.loads(data_line.removeprefix("data: "))

    def socket_is_closed(self, sock: socket.socket) -> bool:
        try:
            data = sock.recv(1)
            return data == b""
        except TimeoutError:
            return False
        except OSError:
            return True

    def test_state_and_sse_include_transport(self) -> None:
        app = self.start_app(open_default_transport=False)
        state = self.fetch_state(app)
        transport = state["transport"]
        self.assertEqual(transport["kind"], "tcp_server")
        self.assertEqual(transport["host"], "127.0.0.1")
        self.assertFalse(transport["listening"])

        event = self.read_first_sse_event(app)
        self.assertIn("state", event)
        self.assertIn("transport", event["state"])

    def test_transport_open_close_and_reopen(self) -> None:
        app = self.start_app(open_default_transport=False)
        port = self.fetch_state(app)["transport"]["port"]

        status, payload = self.request_json(
            app,
            "POST",
            "/api/transport/open",
            {"host": "127.0.0.1", "port": port},
        )
        self.assertEqual(status, 200)
        self.assertTrue(payload["transport"]["listening"])

        with socket.create_connection(("127.0.0.1", int(port)), timeout=1):
            pass

        status, payload = self.request_json(app, "POST", "/api/transport/close", {})
        self.assertEqual(status, 200)
        self.assertFalse(payload["transport"]["listening"])

        with self.assertRaises(OSError):
            socket.create_connection(("127.0.0.1", int(port)), timeout=0.5)

        status, payload = self.request_json(
            app,
            "POST",
            "/api/transport/open",
            {"host": "127.0.0.1", "port": port},
        )
        self.assertEqual(status, 200)
        self.assertTrue(payload["transport"]["listening"])

    def test_transport_invalid_port_and_conflict(self) -> None:
        app = self.start_app(open_default_transport=False)

        status, payload = self.request_json(
            app,
            "POST",
            "/api/transport/open",
            {"host": "127.0.0.1", "port": 70000},
        )
        self.assertEqual(status, 400)
        self.assertIn("port must be within", payload["error"])

        busy_port = _free_port()
        busy_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        busy_sock.bind(("127.0.0.1", busy_port))
        busy_sock.listen()
        self.addCleanup(busy_sock.close)

        status, payload = self.request_json(
            app,
            "POST",
            "/api/transport/open",
            {"host": "127.0.0.1", "port": busy_port},
        )
        self.assertEqual(status, 409)
        self.assertIn("already in use", payload["error"])
        self.assertFalse(payload["transport"]["listening"])

    def test_command_endpoint_accepts_threshold_debounce(self) -> None:
        app = self.start_app(open_default_transport=True)
        port = self.fetch_state(app)["transport"]["port"]

        gateway_sock = socket.create_connection(("127.0.0.1", int(port)), timeout=1)
        self.addCleanup(gateway_sock.close)
        self.wait_until(lambda: bool(self.fetch_state(app)["gateway"]["connected"]))

        status, payload = self.request_json(
            app,
            "POST",
            "/api/command",
            {"t": "set_threshold", "debounce_ms": 300},
        )
        self.assertEqual(status, 200)
        self.assertEqual(payload["payload"]["debounce_ms"], 300)

    def test_alarm_messages_reach_state_and_sse(self) -> None:
        app = self.start_app(open_default_transport=True)
        port = self.fetch_state(app)["transport"]["port"]

        gateway_sock = socket.create_connection(("127.0.0.1", int(port)), timeout=1)
        self.addCleanup(gateway_sock.close)
        self.wait_until(lambda: bool(self.fetch_state(app)["gateway"]["connected"]))

        conn, response = self.open_sse(app)
        self.addCleanup(conn.close)
        initial = self.read_sse_event(response)
        self.assertEqual(initial["kind"], "snapshot")

        gateway_sock.sendall(
            b'{"t":"alarm","type":"light","level":"on","val":120,"threshold":500,"ts":3}\n'
        )

        self.wait_until(lambda: len(self.fetch_state(app)["active_alarms"]) == 1)
        state = self.fetch_state(app)
        self.assertEqual(state["active_alarms"][0]["type"], "light")
        self.assertEqual(state["active_alarms"][0]["level"], "on")

        event = self.read_sse_event(response)
        self.assertEqual(event["kind"], "incoming")
        self.assertEqual(event["payload"]["t"], "alarm")
        self.assertEqual(event["state"]["active_alarms"][0]["type"], "light")

    def test_rebind_disconnects_existing_gateway(self) -> None:
        first_port = _free_port()
        second_port = _free_port()
        app = self.start_app(open_default_transport=False, tcp_port=first_port)

        status, payload = self.request_json(
            app,
            "POST",
            "/api/transport/open",
            {"host": "127.0.0.1", "port": first_port},
        )
        self.assertEqual(status, 200)
        self.assertTrue(payload["transport"]["listening"])

        gateway_sock = socket.create_connection(("127.0.0.1", first_port), timeout=1)
        gateway_sock.settimeout(0.2)
        self.addCleanup(gateway_sock.close)

        self.wait_until(lambda: bool(self.fetch_state(app)["gateway"]["connected"]))

        status, payload = self.request_json(
            app,
            "POST",
            "/api/transport/open",
            {"host": "127.0.0.1", "port": second_port},
        )
        self.assertEqual(status, 200)
        self.assertEqual(payload["transport"]["port"], second_port)

        self.wait_until(lambda: self.socket_is_closed(gateway_sock))
        self.wait_until(lambda: not bool(self.fetch_state(app)["gateway"]["connected"]))

        with socket.create_connection(("127.0.0.1", second_port), timeout=1):
            pass


if __name__ == "__main__":
    unittest.main()
