from __future__ import annotations

import argparse
import json
import logging
import mimetypes
import pathlib
import queue
import socket
import socketserver
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import urlparse

from .protocol import ProtocolError, SequenceAllocator, build_command, encode_json_line
from .state import DashboardState

LOGGER = logging.getLogger("znjj_host")


class TransportConfigError(ValueError):
    """Raised when the UI submits an invalid transport configuration."""


class TransportBindError(RuntimeError):
    """Raised when the TCP transport fails to bind or activate."""


class EventHub:
    def __init__(self, store: DashboardState) -> None:
        self.store = store
        self._subs: set[queue.Queue[dict[str, Any]]] = set()
        self._subs_lock = threading.Lock()
        self._gateway_lock = threading.Lock()
        self._gateway_sock: socket.socket | None = None

    def attach_gateway(self, sock: socket.socket, peer: tuple[str, int]) -> None:
        old_sock: socket.socket | None = None
        peer_str = f"{peer[0]}:{peer[1]}"
        with self._gateway_lock:
            if self._gateway_sock is not None and self._gateway_sock is not sock:
                old_sock = self._gateway_sock
            self._gateway_sock = sock
        if old_sock is not None:
            self._close_socket(old_sock)
        self.store.mark_gateway_connected(peer_str)
        self.publish("gateway", {"connected": True, "peer": peer_str})

    def detach_gateway(self, sock: socket.socket, *, reason: str = "socket_closed") -> None:
        changed = False
        with self._gateway_lock:
            if self._gateway_sock is sock:
                self._gateway_sock = None
                changed = True
        if changed:
            self.store.mark_gateway_disconnected(reason=reason)
            self.publish("gateway", {"connected": False, "reason": reason})

    def disconnect_gateway(self, *, reason: str) -> bool:
        sock: socket.socket | None = None
        with self._gateway_lock:
            if self._gateway_sock is not None:
                sock = self._gateway_sock
                self._gateway_sock = None
        if sock is None:
            return False
        self._close_socket(sock)
        self.store.mark_gateway_disconnected(reason=reason)
        self.publish("gateway", {"connected": False, "reason": reason})
        return True

    def handle_gateway_message(self, payload: dict[str, Any]) -> None:
        self.store.apply_gateway_message(payload)
        self.publish("incoming", payload)

    def send_command(self, payload: dict[str, Any]) -> None:
        encoded = encode_json_line(payload)
        with self._gateway_lock:
            sock = self._gateway_sock
        if sock is None:
            self.store.record_outgoing(payload, delivered=False)
            self.publish("outgoing", {"error": "gateway_not_connected", "payload": payload})
            raise RuntimeError("gateway is not connected")
        try:
            sock.sendall(encoded)
        except OSError as exc:
            self.store.record_outgoing(payload, delivered=False)
            self.publish("outgoing", {"error": str(exc), "payload": payload})
            raise RuntimeError("failed to send command to gateway") from exc

        self.store.record_outgoing(payload, delivered=True)
        self.publish("outgoing", payload)

    def subscribe(self) -> queue.Queue[dict[str, Any]]:
        sub: queue.Queue[dict[str, Any]] = queue.Queue()
        with self._subs_lock:
            self._subs.add(sub)
        return sub

    def unsubscribe(self, sub: queue.Queue[dict[str, Any]]) -> None:
        with self._subs_lock:
            self._subs.discard(sub)

    def publish(self, kind: str, payload: Any) -> None:
        event = {
            "kind": kind,
            "payload": payload,
            "state": self.store.snapshot(),
        }
        with self._subs_lock:
            subscribers = list(self._subs)
        for sub in subscribers:
            sub.put(event)

    def _close_socket(self, sock: socket.socket) -> None:
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            sock.close()
        except OSError:
            pass


class GatewayTCPHandler(socketserver.StreamRequestHandler):
    server: "GatewayTCPServer"

    def handle(self) -> None:
        hub = self.server.hub
        hub.attach_gateway(self.request, self.client_address)
        LOGGER.info("gateway connected from %s:%s", *self.client_address)
        try:
            try:
                for raw_line in self.rfile:
                    line = raw_line.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    try:
                        payload = json.loads(line)
                    except json.JSONDecodeError:
                        LOGGER.warning("invalid gateway JSON: %s", line)
                        hub.store.record_host_event(
                            "invalid gateway JSON received",
                            {"line": line},
                            event_type="gateway-json-error",
                        )
                        hub.publish("gateway-error", {"line": line})
                        continue
                    hub.handle_gateway_message(payload)
            except OSError as exc:
                LOGGER.debug("gateway socket read ended with %s", exc)
        finally:
            LOGGER.info("gateway connection closed from %s:%s", *self.client_address)
            hub.detach_gateway(self.request)


class GatewayTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, server_address: tuple[str, int], hub: EventHub) -> None:
        self.hub = hub
        super().__init__(server_address, GatewayTCPHandler)


class DashboardHTTPServer(ThreadingHTTPServer):
    daemon_threads = True


class ManagedGatewayTransport:
    def __init__(self, hub: EventHub, *, default_host: str, default_port: int) -> None:
        self._hub = hub
        self._lock = threading.RLock()
        self._server: GatewayTCPServer | None = None
        self._thread: threading.Thread | None = None
        self._host = _coerce_transport_host(default_host)
        self._port = _coerce_transport_port(default_port)

    def open(self, *, host: str, port: int) -> dict[str, Any]:
        host = _coerce_transport_host(host)
        port = _coerce_transport_port(port)

        with self._lock:
            if self._server is not None and self._host == host and self._port == port:
                return self._hub.store.transport_snapshot()

        try:
            new_server = GatewayTCPServer((host, port), self._hub)
        except OSError as exc:
            message = _describe_transport_error(exc, host, port)
            with self._lock:
                has_active_listener = self._server is not None
            if has_active_listener:
                self._hub.store.record_transport_error(message)
            else:
                self._hub.store.mark_transport_closed(host=host, port=port, error=message)
            self._hub.publish("transport-error", {"host": host, "port": port, "error": message})
            raise TransportBindError(message) from exc

        new_thread = threading.Thread(
            target=new_server.serve_forever,
            name=f"znjj-tcp-{host}:{port}",
            daemon=True,
        )
        new_thread.start()

        with self._lock:
            old_server = self._server
            old_thread = self._thread
            self._server = new_server
            self._thread = new_thread
            self._host = host
            self._port = port

        self._hub.store.mark_transport_listening(host, port)
        self._hub.publish("transport", self._hub.store.transport_snapshot())

        if old_server is not None:
            self._hub.disconnect_gateway(reason="transport_rebound")
            self._shutdown_server(old_server, old_thread)

        return self._hub.store.transport_snapshot()

    def close(self) -> dict[str, Any]:
        with self._lock:
            old_server = self._server
            old_thread = self._thread
            host = self._host
            port = self._port
            self._server = None
            self._thread = None

        self._hub.disconnect_gateway(reason="transport_closed")
        if old_server is not None:
            self._shutdown_server(old_server, old_thread)

        self._hub.store.mark_transport_closed(host=host, port=port, error=None)
        self._hub.publish("transport", self._hub.store.transport_snapshot())
        return self._hub.store.transport_snapshot()

    def stop(self) -> None:
        self.close()

    def _shutdown_server(
        self,
        server: GatewayTCPServer | None,
        thread: threading.Thread | None,
    ) -> None:
        if server is None:
            return
        server.shutdown()
        server.server_close()
        if thread is not None and thread.is_alive():
            thread.join(timeout=2.0)


def make_http_handler(
    hub: EventHub,
    static_dir: pathlib.Path,
    allocator: SequenceAllocator,
    transport: ManagedGatewayTransport,
) -> type[BaseHTTPRequestHandler]:
    class DashboardHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self) -> None:  # noqa: N802
            parsed = urlparse(self.path)
            if parsed.path == "/api/state":
                self._send_json(HTTPStatus.OK, hub.store.snapshot())
                return
            if parsed.path == "/api/events":
                self._handle_events()
                return
            if parsed.path == "/":
                self._serve_static("index.html")
                return
            if parsed.path.startswith("/static/"):
                self._serve_static(parsed.path.removeprefix("/static/"))
                return
            self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found"})

        def do_POST(self) -> None:  # noqa: N802
            parsed = urlparse(self.path)

            if parsed.path == "/api/command":
                self._handle_command_post()
                return
            if parsed.path == "/api/transport/open":
                self._handle_transport_open()
                return
            if parsed.path == "/api/transport/close":
                payload = transport.close()
                self._send_json(HTTPStatus.OK, {"ok": True, "transport": payload})
                return

            self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found"})

        def log_message(self, fmt: str, *args: Any) -> None:
            LOGGER.info("http %s", fmt % args)

        def _handle_command_post(self) -> None:
            try:
                body = self._read_json_body()
                message_type = str(body.get("t", "")).strip()
                seq = body.get("seq")
                if seq is None:
                    seq = allocator.next()
                fields = {key: value for key, value in body.items() if key not in {"t", "seq"}}
                payload = build_command(message_type, seq=seq, **fields)
                hub.send_command(payload)
            except ProtocolError as exc:
                self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(exc)})
                return
            except RuntimeError as exc:
                self._send_json(HTTPStatus.SERVICE_UNAVAILABLE, {"ok": False, "error": str(exc)})
                return
            except json.JSONDecodeError:
                self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "invalid_json"})
                return

            self._send_json(HTTPStatus.OK, {"ok": True, "payload": payload})

        def _handle_transport_open(self) -> None:
            try:
                body = self._read_json_body()
                host = body.get("host")
                port = body.get("port")
                payload = transport.open(host=host, port=port)
            except json.JSONDecodeError:
                self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "invalid_json"})
                return
            except TransportConfigError as exc:
                self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(exc)})
                return
            except TransportBindError as exc:
                self._send_json(
                    HTTPStatus.CONFLICT,
                    {"ok": False, "error": str(exc), "transport": hub.store.transport_snapshot()},
                )
                return

            self._send_json(HTTPStatus.OK, {"ok": True, "transport": payload})

        def _read_json_body(self) -> dict[str, Any]:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            if not raw:
                return {}
            return json.loads(raw.decode("utf-8"))

        def _send_json(self, status: HTTPStatus, payload: Any) -> None:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status.value)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _handle_events(self) -> None:
            sub = hub.subscribe()
            self.send_response(HTTPStatus.OK.value)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()

            try:
                self._write_sse({"kind": "snapshot", "payload": None, "state": hub.store.snapshot()})
                while True:
                    try:
                        event = sub.get(timeout=15.0)
                        self._write_sse(event)
                    except queue.Empty:
                        self.wfile.write(b": keep-alive\n\n")
                        self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                hub.unsubscribe(sub)

        def _write_sse(self, event: dict[str, Any]) -> None:
            data = json.dumps(event, ensure_ascii=False)
            payload = f"event: state\ndata: {data}\n\n".encode("utf-8")
            self.wfile.write(payload)
            self.wfile.flush()

        def _serve_static(self, relative_path: str) -> None:
            requested = (static_dir / relative_path).resolve()
            if not str(requested).startswith(str(static_dir.resolve())) or not requested.is_file():
                self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found"})
                return
            content = requested.read_bytes()
            mime_type, _ = mimetypes.guess_type(str(requested))
            content_type = mime_type or "application/octet-stream"
            if content_type.startswith("text/") or content_type in {
                "application/javascript",
                "application/json",
                "image/svg+xml",
            }:
                content_type = f"{content_type}; charset=utf-8"
            self.send_response(HTTPStatus.OK.value)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)

    return DashboardHandler


class HostApplication:
    def __init__(
        self,
        *,
        tcp_host: str,
        tcp_port: int,
        http_host: str,
        http_port: int,
        static_dir: pathlib.Path | None = None,
    ) -> None:
        root_dir = pathlib.Path(__file__).resolve().parents[1]
        self.static_dir = static_dir or (root_dir / "static")
        self.default_tcp_host = _coerce_transport_host(tcp_host)
        self.default_tcp_port = _coerce_transport_port(tcp_port)

        self.store = DashboardState(
            transport_host=self.default_tcp_host,
            transport_port=self.default_tcp_port,
        )
        self.allocator = SequenceAllocator()
        self.hub = EventHub(self.store)
        self.transport = ManagedGatewayTransport(
            self.hub,
            default_host=self.default_tcp_host,
            default_port=self.default_tcp_port,
        )

        handler = make_http_handler(self.hub, self.static_dir, self.allocator, self.transport)
        self.http_server = DashboardHTTPServer((http_host, http_port), handler)
        self._http_thread: threading.Thread | None = None

    @property
    def http_address(self) -> tuple[str, int]:
        host, port = self.http_server.server_address[:2]
        return str(host), int(port)

    def start(self, *, open_default_transport: bool = True) -> None:
        if self._http_thread is None:
            self._http_thread = threading.Thread(
                target=self.http_server.serve_forever,
                name="znjj-http",
                daemon=True,
            )
            self._http_thread.start()

        if open_default_transport:
            try:
                self.transport.open(host=self.default_tcp_host, port=self.default_tcp_port)
            except TransportBindError as exc:
                LOGGER.warning("default TCP listener not started: %s", exc)

    def stop(self) -> None:
        self.transport.stop()
        self.http_server.shutdown()
        self.http_server.server_close()
        if self._http_thread is not None and self._http_thread.is_alive():
            self._http_thread.join(timeout=2.0)
        self._http_thread = None


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="ZNJJ PC host dashboard")
    parser.add_argument("--tcp-host", default="0.0.0.0", help="host for the gateway TCP server")
    parser.add_argument("--tcp-port", default=23333, type=int, help="port for the gateway TCP server")
    parser.add_argument("--http-host", default="127.0.0.1", help="host for the dashboard HTTP server")
    parser.add_argument("--http-port", default=8080, type=int, help="port for the dashboard HTTP server")
    parser.add_argument("--log-level", default="INFO", help="logging level, e.g. DEBUG/INFO/WARNING")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    app = HostApplication(
        tcp_host=args.tcp_host,
        tcp_port=args.tcp_port,
        http_host=args.http_host,
        http_port=args.http_port,
    )
    app.start(open_default_transport=True)

    http_host, http_port = app.http_address
    LOGGER.info("dashboard HTTP server listening on http://%s:%s", http_host, http_port)
    LOGGER.info("gateway TCP target is %s:%s", args.tcp_host, args.tcp_port)

    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        LOGGER.info("shutting down servers")
    finally:
        app.stop()

    return 0


def _coerce_transport_host(host: Any) -> str:
    if not isinstance(host, str):
        raise TransportConfigError("host must be a string")
    value = host.strip()
    if not value:
        raise TransportConfigError("host must not be empty")
    return value


def _coerce_transport_port(port: Any) -> int:
    try:
        value = int(port)
    except (TypeError, ValueError) as exc:
        raise TransportConfigError("port must be an integer") from exc
    if value < 1 or value > 65535:
        raise TransportConfigError("port must be within 1..65535")
    return value


def _describe_transport_error(exc: OSError, host: str, port: int) -> str:
    winerror = getattr(exc, "winerror", None)
    if isinstance(exc, OSError) and exc.errno in {98, 10048}:
        return f"port {port} is already in use on {host}"
    if winerror in {10013, 10048}:
        return f"port {port} is already in use or blocked on {host}"
    return f"failed to listen on {host}:{port}: {exc}"


if __name__ == "__main__":
    raise SystemExit(main())
