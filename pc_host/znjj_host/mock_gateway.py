from __future__ import annotations

import argparse
import json
import math
import random
import socket
import time
from typing import Any

from .protocol import DEFAULT_THRESHOLDS, encode_json_line

NODE1_ID = 0x0101
NODE2_ID = 0x0102


class MockGateway:
    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self.thresholds = dict(DEFAULT_THRESHOLDS)
        self.node_states: dict[int, dict[str, Any]] = {
            NODE1_ID: {
                "role": "temp_hum",
                "dev": "enddev",
                "online": True,
                "rssi": -44,
                "last_seen_s": 0,
                "temp": 26.4,
                "hum": 58,
                "led": "off",
                "buzzer": "off",
            },
            NODE2_ID: {
                "role": "lux",
                "dev": "enddev",
                "online": True,
                "rssi": -50,
                "last_seen_s": 0,
                "lux": 620,
                "led": "off",
                "buzzer": "off",
            },
        }
        self.light_alarm_on = False
        self.temp_alarm_on = False

    def run(self) -> None:
        while True:
            try:
                self._run_once()
            except OSError as exc:
                print(f"[mock] connection error: {exc}; retry in 2s")
                time.sleep(2.0)

    def _run_once(self) -> None:
        with socket.create_connection((self.host, self.port), timeout=10.0) as sock:
            print(f"[mock] connected to {self.host}:{self.port}")
            sock.settimeout(0.2)
            self._send(sock, {"t": "hello", "fw": "znjj-mock-1.0", "gw_id": 99})
            self._send(sock, {"t": "net", "state": 9, "channel": 15, "panid": 4660, "joined": 2})
            self._send_node_info(sock, NODE1_ID)
            self._send_node_info(sock, NODE2_ID)
            self._send_status(sock, NODE1_ID)
            self._send_status(sock, NODE2_ID)

            start = time.monotonic()
            last_temp_report = 0.0
            last_lux_report = 0.0
            last_ping = 0.0
            rx_buf = bytearray()

            while True:
                now = time.monotonic()
                if now - last_temp_report >= 2.0:
                    self._tick_temp_hum(now - start)
                    self._send_temp_hum(sock)
                    last_temp_report = now
                if now - last_lux_report >= 0.5:
                    self._tick_lux(now - start)
                    self._send_lux(sock)
                    last_lux_report = now
                if now - last_ping >= 5.0:
                    self._send(sock, {"t": "pong", "ts": _ts_ms()})
                    last_ping = now

                self._drive_automation(sock)
                self._drain_commands(sock, rx_buf)
                time.sleep(0.05)

    def _send(self, sock: socket.socket, payload: dict[str, Any]) -> None:
        sock.sendall(encode_json_line(payload))

    def _send_node_info(self, sock: socket.socket, node_id: int) -> None:
        node = self.node_states[node_id]
        self._send(
            sock,
            {
                "t": "node_info",
                "node": node_id,
                "role": node["role"],
                "dev": node["dev"],
                "rssi": node["rssi"],
                "last_seen_s": node["last_seen_s"],
                "online": node["online"],
            },
        )

    def _send_temp_hum(self, sock: socket.socket) -> None:
        node = self.node_states[NODE1_ID]
        self._send(
            sock,
            {
                "t": "report",
                "node": NODE1_ID,
                "kind": "temp_hum",
                "temp": round(node["temp"], 2),
                "hum": int(node["hum"]),
                "ts": _ts_ms(),
            },
        )

    def _send_lux(self, sock: socket.socket) -> None:
        node = self.node_states[NODE2_ID]
        self._send(
            sock,
            {
                "t": "report",
                "node": NODE2_ID,
                "kind": "lux",
                "lux": int(node["lux"]),
                "ts": _ts_ms(),
            },
        )

    def _tick_temp_hum(self, elapsed: float) -> None:
        node = self.node_states[NODE1_ID]
        node["temp"] = 27.0 + 7.5 * math.sin(elapsed / 12.0) + random.uniform(-0.3, 0.3)
        node["hum"] = 56 + 18 * math.sin(elapsed / 10.0 + 1.2) + random.uniform(-1.5, 1.5)

    def _tick_lux(self, elapsed: float) -> None:
        node = self.node_states[NODE2_ID]
        node["lux"] = max(60, int(700 + 650 * math.sin(elapsed / 8.0) + random.uniform(-20, 20)))

    def _drive_automation(self, sock: socket.socket) -> None:
        node1 = self.node_states[NODE1_ID]
        node2 = self.node_states[NODE2_ID]

        if node2["lux"] < self.thresholds["lux_low"] and not self.light_alarm_on:
            self.light_alarm_on = True
            node2["led"] = "on"
            self._send_alarm(sock, "light", "on", int(node2["lux"]), self.thresholds["lux_low"])
            self._send_status(sock, NODE2_ID)
        elif node2["lux"] > self.thresholds["lux_low"] + 50 and self.light_alarm_on:
            self.light_alarm_on = False
            node2["led"] = "off"
            self._send_alarm(sock, "light", "off", int(node2["lux"]), self.thresholds["lux_low"])
            self._send_status(sock, NODE2_ID)

        temp_out = node1["temp"] > self.thresholds["temp_high"] or node1["temp"] < self.thresholds["temp_low"]
        hum_out = node1["hum"] > self.thresholds["hum_high"] or node1["hum"] < self.thresholds["hum_low"]
        if (temp_out or hum_out) and not self.temp_alarm_on:
            self.temp_alarm_on = True
            node1["buzzer"] = "on"
            if temp_out:
                threshold = self.thresholds["temp_high"] if node1["temp"] > self.thresholds["temp_high"] else self.thresholds["temp_low"]
                self._send_alarm(sock, "temp", "on", round(node1["temp"], 2), threshold)
            else:
                threshold = self.thresholds["hum_high"] if node1["hum"] > self.thresholds["hum_high"] else self.thresholds["hum_low"]
                self._send_alarm(sock, "hum", "on", int(node1["hum"]), threshold)
            self._send_status(sock, NODE1_ID)
        elif not temp_out and not hum_out and self.temp_alarm_on:
            self.temp_alarm_on = False
            node1["buzzer"] = "off"
            self._send_alarm(sock, "temp", "off", round(node1["temp"], 2), self.thresholds["temp_high"])
            self._send_status(sock, NODE1_ID)

    def _send_alarm(self, sock: socket.socket, alarm_type: str, level: str, val: Any, threshold: Any) -> None:
        self._send(
            sock,
            {
                "t": "alarm",
                "type": alarm_type,
                "level": level,
                "val": val,
                "threshold": threshold,
                "ts": _ts_ms(),
            },
        )

    def _send_status(self, sock: socket.socket, node_id: int) -> None:
        node = self.node_states[node_id]
        self._send(
            sock,
            {
                "t": "status",
                "node": node_id,
                "led": node["led"],
                "buzzer": node["buzzer"],
                "ts": _ts_ms(),
            },
        )

    def _drain_commands(self, sock: socket.socket, rx_buf: bytearray) -> None:
        try:
            chunk = sock.recv(4096)
        except (TimeoutError, socket.timeout):
            return
        if not chunk:
            raise OSError("server closed connection")
        rx_buf.extend(chunk)

        while True:
            nl = rx_buf.find(b"\n")
            if nl < 0:
                break
            raw = bytes(rx_buf[:nl]).strip()
            del rx_buf[: nl + 1]
            if not raw:
                continue
            payload = json.loads(raw.decode("utf-8"))
            self._handle_command(sock, payload)

    def _handle_command(self, sock: socket.socket, payload: dict[str, Any]) -> None:
        message_type = payload.get("t")
        seq = payload.get("seq", 0)

        if message_type == "cmd":
            node_id = self._resolve_node(payload.get("node"))
            target = payload.get("target")
            op = payload.get("op")
            node = self.node_states[node_id]
            current = node[target]
            if op == "toggle":
                node[target] = "off" if current == "on" else "on"
            else:
                node[target] = str(op)
            self._send(sock, {"t": "ack", "seq": seq, "ok": True})
            self._send_status(sock, node_id)
            return

        if message_type == "set_threshold":
            for key in DEFAULT_THRESHOLDS:
                if key in payload:
                    self.thresholds[key] = payload[key]
            self._send(sock, {"t": "ack", "seq": seq, "ok": True})
            return

        if message_type == "allow_join":
            self._send(sock, {"t": "ack", "seq": seq, "ok": True})
            self._send(sock, {"t": "net", "state": 9, "channel": 15, "panid": 4660, "joined": 2})
            return

        if message_type == "list_nodes":
            self._send_node_info(sock, NODE1_ID)
            self._send_node_info(sock, NODE2_ID)
            return

        if message_type == "ping":
            self._send(sock, {"t": "pong", "ts": _ts_ms()})
            return

        self._send(sock, {"t": "ack", "seq": seq, "ok": False, "err": "type"})

    def _resolve_node(self, node: Any) -> int:
        node_int = int(node)
        if node_int == 1:
            return NODE1_ID
        if node_int == 2:
            return NODE2_ID
        if node_int not in self.node_states:
            raise ValueError(f"unknown node: {node_int}")
        return node_int


def _ts_ms() -> int:
    return int(time.time() * 1000)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mock gateway for the ZNJJ PC dashboard")
    parser.add_argument("--host", default="127.0.0.1", help="dashboard TCP host")
    parser.add_argument("--port", default=23333, type=int, help="dashboard TCP port")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    gateway = MockGateway(args.host, args.port)
    gateway.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
