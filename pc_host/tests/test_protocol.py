from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from znjj_host.protocol import ProtocolError, build_command  # noqa: E402
from znjj_host.state import DashboardState  # noqa: E402


class ProtocolTests(unittest.TestCase):
    def test_build_cmd_payload(self) -> None:
        payload = build_command("cmd", seq=7, node=1, target="led", op="toggle")
        self.assertEqual(
            payload,
            {"t": "cmd", "seq": 7, "node": 1, "target": "led", "op": "toggle"},
        )

    def test_build_threshold_payload(self) -> None:
        payload = build_command("set_threshold", seq=8, lux_low="480", temp_high="31.5")
        self.assertEqual(payload["lux_low"], 480)
        self.assertEqual(payload["temp_high"], 31.5)

    def test_invalid_target_raises(self) -> None:
        with self.assertRaises(ProtocolError):
            build_command("cmd", seq=1, node=1, target="fan", op="on")


class DashboardStateTests(unittest.TestCase):
    def test_apply_messages_updates_node_snapshot(self) -> None:
        store = DashboardState()
        store.apply_gateway_message({"t": "report", "node": 257, "kind": "temp_hum", "temp": 26.4, "hum": 58, "ts": 1})
        store.apply_gateway_message({"t": "status", "node": 257, "led": "on", "buzzer": "off", "ts": 2})
        store.apply_gateway_message(
            {"t": "node_info", "node": 257, "role": "temp_hum", "dev": "enddev", "rssi": -41, "last_seen_s": 0, "online": 1}
        )
        snapshot = store.snapshot()
        self.assertEqual(snapshot["aliases"]["node1"], 257)
        self.assertEqual(snapshot["nodes"][0]["temp"], 26.4)
        self.assertEqual(snapshot["nodes"][0]["led"], "on")
        self.assertEqual(snapshot["nodes"][0]["rssi"], -41)

    def test_alarm_tracking(self) -> None:
        store = DashboardState()
        store.apply_gateway_message({"t": "alarm", "type": "light", "level": "on", "val": 120, "threshold": 500, "ts": 3})
        self.assertEqual(len(store.snapshot()["active_alarms"]), 1)
        store.apply_gateway_message({"t": "alarm", "type": "light", "level": "off", "val": 600, "threshold": 500, "ts": 4})
        self.assertEqual(len(store.snapshot()["active_alarms"]), 0)


if __name__ == "__main__":
    unittest.main()

