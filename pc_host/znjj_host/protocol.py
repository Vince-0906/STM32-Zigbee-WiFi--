from __future__ import annotations

import json
import threading
from typing import Any

DEFAULT_THRESHOLDS = {
    "lux_low": 500,
    "temp_high": 32.0,
    "temp_low": 5.0,
    "hum_high": 85.0,
    "hum_low": 20.0,
}

_CMD_FIELDS = {"node", "target", "op"}
_THRESHOLD_FIELDS = set(DEFAULT_THRESHOLDS)
_TARGETS = {"led", "buzzer"}
_OPS = {"on", "off", "toggle"}


class ProtocolError(ValueError):
    """Raised when the UI asks to build an invalid protocol message."""


class SequenceAllocator:
    """Thread-safe incremental sequence generator for PC -> gateway messages."""

    def __init__(self, start: int = 1) -> None:
        self._lock = threading.Lock()
        self._next = start

    def next(self) -> int:
        with self._lock:
            value = self._next
            self._next += 1
            return value


def encode_json_line(payload: dict[str, Any]) -> bytes:
    return (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def build_command(message_type: str, *, seq: int | None = None, **fields: Any) -> dict[str, Any]:
    payload: dict[str, Any] = {"t": message_type}
    if seq is not None:
        payload["seq"] = int(seq)

    if message_type == "cmd":
        _populate_cmd(payload, fields)
    elif message_type == "set_threshold":
        _populate_thresholds(payload, fields)
    elif message_type == "allow_join":
        _populate_allow_join(payload, fields)
    elif message_type in {"list_nodes", "ping"}:
        _validate_no_extra(fields, message_type)
    else:
        raise ProtocolError(f"unsupported command type: {message_type}")

    return payload


def _populate_cmd(payload: dict[str, Any], fields: dict[str, Any]) -> None:
    missing = _CMD_FIELDS.difference(fields)
    if missing:
        raise ProtocolError(f"cmd is missing fields: {', '.join(sorted(missing))}")

    node = _coerce_int(fields["node"], "node")
    if node < 1 or node > 0xFFFF:
        raise ProtocolError("node must be within 1..65535")

    target = str(fields["target"]).strip().lower()
    if target not in _TARGETS:
        raise ProtocolError("target must be one of: led, buzzer")

    op = str(fields["op"]).strip().lower()
    if op not in _OPS:
        raise ProtocolError("op must be one of: on, off, toggle")

    payload["node"] = node
    payload["target"] = target
    payload["op"] = op

    extra = set(fields).difference(_CMD_FIELDS)
    if extra:
        raise ProtocolError(f"cmd has unsupported fields: {', '.join(sorted(extra))}")


def _populate_thresholds(payload: dict[str, Any], fields: dict[str, Any]) -> None:
    supported = _THRESHOLD_FIELDS
    provided = supported.intersection(fields)
    if not provided:
        raise ProtocolError("set_threshold requires at least one threshold field")

    for key in provided:
        if key == "lux_low":
            value = _coerce_int(fields[key], key)
            if value < 0:
                raise ProtocolError("lux_low must be >= 0")
            payload[key] = value
        else:
            payload[key] = round(_coerce_float(fields[key], key), 2)

    extra = set(fields).difference(supported)
    if extra:
        raise ProtocolError(f"set_threshold has unsupported fields: {', '.join(sorted(extra))}")


def _populate_allow_join(payload: dict[str, Any], fields: dict[str, Any]) -> None:
    if "sec" not in fields:
        raise ProtocolError("allow_join requires sec")
    sec = _coerce_int(fields["sec"], "sec")
    if sec < 0 or sec > 255:
        raise ProtocolError("sec must be within 0..255")
    payload["sec"] = sec

    extra = set(fields).difference({"sec"})
    if extra:
        raise ProtocolError(f"allow_join has unsupported fields: {', '.join(sorted(extra))}")


def _validate_no_extra(fields: dict[str, Any], message_type: str) -> None:
    if fields:
        raise ProtocolError(f"{message_type} does not take extra fields")


def _coerce_int(value: Any, field_name: str) -> int:
    try:
        return int(value)
    except (TypeError, ValueError) as exc:
        raise ProtocolError(f"{field_name} must be an integer") from exc


def _coerce_float(value: Any, field_name: str) -> float:
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ProtocolError(f"{field_name} must be a number") from exc

