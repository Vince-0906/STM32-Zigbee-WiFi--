from __future__ import annotations

import copy
import threading
import time
from collections import deque
from dataclasses import asdict, dataclass
from typing import Any

from .protocol import DEFAULT_THRESHOLDS


@dataclass
class NodeSnapshot:
    node: int
    role: str = "unknown"
    dev: str = "unknown"
    online: bool = False
    temp: float | None = None
    hum: int | None = None
    lux: int | None = None
    led: str = "unknown"
    buzzer: str = "unknown"
    rssi: int | None = None
    last_seen_s: int | None = None
    last_update_ts: int | None = None


class DashboardState:
    """Thread-safe in-memory state for the PC dashboard."""

    def __init__(
        self,
        *,
        transport_host: str = "0.0.0.0",
        transport_port: int = 23333,
        max_logs: int = 200,
        max_alarm_history: int = 50,
    ) -> None:
        self._lock = threading.RLock()
        self._nodes: dict[int, NodeSnapshot] = {}
        self._network: dict[str, Any] = {
            "state": None,
            "channel": None,
            "panid": None,
            "joined": None,
        }
        self._thresholds: dict[str, Any] = dict(DEFAULT_THRESHOLDS)
        self._logs: deque[dict[str, Any]] = deque(maxlen=max_logs)
        self._alarm_history: deque[dict[str, Any]] = deque(maxlen=max_alarm_history)
        self._active_alarms: dict[str, dict[str, Any]] = {}
        self._gateway: dict[str, Any] = {
            "connected": False,
            "peer": None,
            "fw": None,
            "gw_id": None,
            "last_seen_ms": None,
        }
        self._transport: dict[str, Any] = {
            "kind": "tcp_server",
            "host": transport_host,
            "port": int(transport_port),
            "listening": False,
            "last_error": None,
            "last_action_ms": 0,
        }
        self._last_ack: dict[str, Any] | None = None
        self._last_pong: dict[str, Any] | None = None

    def mark_gateway_connected(self, peer: str) -> None:
        with self._lock:
            self._gateway["connected"] = True
            self._gateway["peer"] = peer
            self._gateway["last_seen_ms"] = _now_ms()
            self._log_locked("system", "gateway", f"gateway connected from {peer}", {"peer": peer})

    def mark_gateway_disconnected(self, *, reason: str | None = None) -> None:
        with self._lock:
            peer = self._gateway.get("peer")
            self._gateway["connected"] = False
            self._gateway["peer"] = None
            self._gateway["last_seen_ms"] = _now_ms()
            summary = f"gateway disconnected from {peer or 'unknown'}"
            if reason:
                summary = f"{summary} ({reason})"
            self._log_locked("system", "gateway", summary, {"peer": peer, "reason": reason})

    def mark_transport_listening(self, host: str, port: int) -> None:
        with self._lock:
            self._transport["host"] = host
            self._transport["port"] = int(port)
            self._transport["listening"] = True
            self._transport["last_error"] = None
            self._transport["last_action_ms"] = _now_ms()
            self._log_locked(
                "system",
                "transport",
                f"transport listening on {host}:{port}",
                copy.deepcopy(self._transport),
            )

    def mark_transport_closed(
        self,
        *,
        host: str | None = None,
        port: int | None = None,
        error: str | None = None,
    ) -> None:
        with self._lock:
            if host is not None:
                self._transport["host"] = host
            if port is not None:
                self._transport["port"] = int(port)
            self._transport["listening"] = False
            self._transport["last_error"] = error
            self._transport["last_action_ms"] = _now_ms()
            summary = f"transport closed on {self._transport['host']}:{self._transport['port']}"
            if error:
                summary = f"{summary} ({error})"
            self._log_locked(
                "system",
                "transport",
                summary,
                copy.deepcopy(self._transport),
            )

    def record_transport_error(self, error: str) -> None:
        with self._lock:
            self._transport["last_error"] = error
            self._transport["last_action_ms"] = _now_ms()
            self._log_locked(
                "system",
                "transport-error",
                f"transport error: {error}",
                copy.deepcopy(self._transport),
            )

    def record_outgoing(self, payload: dict[str, Any], *, delivered: bool) -> None:
        with self._lock:
            summary = _message_summary(payload)
            event_type = "pc->gw" if delivered else "pc->gw-error"
            self._log_locked("outgoing", event_type, summary, payload)
            if payload.get("t") == "set_threshold":
                for key in DEFAULT_THRESHOLDS:
                    if key in payload:
                        self._thresholds[key] = payload[key]

    def record_host_event(self, summary: str, payload: Any = None, *, event_type: str = "host") -> None:
        with self._lock:
            self._log_locked("system", event_type, summary, payload)

    def apply_gateway_message(self, payload: dict[str, Any]) -> None:
        with self._lock:
            self._gateway["last_seen_ms"] = _now_ms()
            message_type = str(payload.get("t", "unknown"))

            if message_type == "hello":
                self._gateway["fw"] = payload.get("fw")
                self._gateway["gw_id"] = payload.get("gw_id")
            elif message_type == "report":
                self._update_node_report_locked(payload)
            elif message_type == "status":
                self._update_node_status_locked(payload)
            elif message_type == "node_info":
                self._update_node_info_locked(payload)
            elif message_type == "net":
                self._network["state"] = payload.get("state")
                self._network["channel"] = payload.get("channel")
                self._network["panid"] = payload.get("panid")
                self._network["joined"] = payload.get("joined")
            elif message_type == "alarm":
                self._update_alarm_locked(payload)
            elif message_type == "ack":
                self._last_ack = copy.deepcopy(payload)
            elif message_type == "pong":
                self._last_pong = copy.deepcopy(payload)

            self._log_locked("incoming", message_type, _message_summary(payload), payload)

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            nodes = [asdict(node) for node in sorted(self._nodes.values(), key=lambda item: item.node)]
            aliases = {"node1": None, "node2": None}
            for node in nodes:
                if node["role"] == "temp_hum" and aliases["node1"] is None:
                    aliases["node1"] = node["node"]
                if node["role"] == "lux" and aliases["node2"] is None:
                    aliases["node2"] = node["node"]
            return {
                "generated_at_ms": _now_ms(),
                "transport": copy.deepcopy(self._transport),
                "gateway": copy.deepcopy(self._gateway),
                "network": copy.deepcopy(self._network),
                "thresholds": copy.deepcopy(self._thresholds),
                "nodes": nodes,
                "aliases": aliases,
                "active_alarms": list(self._active_alarms.values()),
                "alarm_history": list(self._alarm_history),
                "last_ack": copy.deepcopy(self._last_ack),
                "last_pong": copy.deepcopy(self._last_pong),
                "logs": list(self._logs),
            }

    def transport_snapshot(self) -> dict[str, Any]:
        with self._lock:
            return copy.deepcopy(self._transport)

    def _update_node_report_locked(self, payload: dict[str, Any]) -> None:
        node = self._ensure_node_locked(payload.get("node"))
        kind = payload.get("kind")
        node.online = True
        node.last_update_ts = payload.get("ts")
        if kind == "temp_hum":
            node.role = "temp_hum"
            node.temp = payload.get("temp")
            node.hum = payload.get("hum")
        elif kind == "lux":
            node.role = "lux"
            node.lux = payload.get("lux")

    def _update_node_status_locked(self, payload: dict[str, Any]) -> None:
        node = self._ensure_node_locked(payload.get("node"))
        node.online = True
        node.led = str(payload.get("led", "unknown"))
        node.buzzer = str(payload.get("buzzer", "unknown"))
        node.last_update_ts = payload.get("ts")

    def _update_node_info_locked(self, payload: dict[str, Any]) -> None:
        node = self._ensure_node_locked(payload.get("node"))
        node.role = str(payload.get("role", node.role))
        node.dev = str(payload.get("dev", node.dev))
        node.rssi = payload.get("rssi")
        node.last_seen_s = payload.get("last_seen_s")
        if "online" in payload:
            node.online = bool(payload["online"])

    def _update_alarm_locked(self, payload: dict[str, Any]) -> None:
        record = {
            "type": payload.get("type"),
            "level": payload.get("level"),
            "val": payload.get("val"),
            "threshold": payload.get("threshold"),
            "ts": payload.get("ts"),
        }
        alarm_type = str(payload.get("type", "unknown"))
        if payload.get("level") == "on":
            self._active_alarms[alarm_type] = record
        else:
            self._active_alarms.pop(alarm_type, None)
        self._alarm_history.appendleft(record)

    def _ensure_node_locked(self, node_id: Any) -> NodeSnapshot:
        node_int = int(node_id)
        node = self._nodes.get(node_int)
        if node is None:
            node = NodeSnapshot(node=node_int)
            self._nodes[node_int] = node
        return node

    def _log_locked(
        self,
        direction: str,
        event_type: str,
        summary: str,
        payload: Any,
    ) -> None:
        self._logs.appendleft(
            {
                "ts": _now_ms(),
                "direction": direction,
                "type": event_type,
                "summary": summary,
                "payload": copy.deepcopy(payload),
            }
        )


def _message_summary(payload: dict[str, Any]) -> str:
    message_type = str(payload.get("t", "unknown"))
    if message_type == "hello":
        return f"hello fw={payload.get('fw')} gw_id={payload.get('gw_id')}"
    if message_type == "report":
        kind = payload.get("kind")
        if kind == "temp_hum":
            return f"report node={payload.get('node')} temp={payload.get('temp')} hum={payload.get('hum')}"
        return f"report node={payload.get('node')} lux={payload.get('lux')}"
    if message_type == "status":
        return (
            f"status node={payload.get('node')} led={payload.get('led')} "
            f"buzzer={payload.get('buzzer')}"
        )
    if message_type == "alarm":
        return f"alarm {payload.get('type')} {payload.get('level')} val={payload.get('val')}"
    if message_type == "net":
        return (
            f"net state={payload.get('state')} ch={payload.get('channel')} "
            f"panid={payload.get('panid')} joined={payload.get('joined')}"
        )
    if message_type == "ack":
        return f"ack seq={payload.get('seq')} ok={payload.get('ok')}"
    if message_type == "pong":
        return f"pong ts={payload.get('ts')}"
    if message_type == "node_info":
        return (
            f"node_info node={payload.get('node')} role={payload.get('role')} "
            f"online={payload.get('online')}"
        )
    if message_type == "cmd":
        return (
            f"cmd seq={payload.get('seq')} node={payload.get('node')} "
            f"{payload.get('target')}={payload.get('op')}"
        )
    if message_type == "set_threshold":
        return f"set_threshold seq={payload.get('seq')}"
    if message_type == "allow_join":
        return f"allow_join seq={payload.get('seq')} sec={payload.get('sec')}"
    if message_type in {"list_nodes", "ping"}:
        return f"{message_type} seq={payload.get('seq')}"
    return message_type


def _now_ms() -> int:
    return int(time.time() * 1000)
