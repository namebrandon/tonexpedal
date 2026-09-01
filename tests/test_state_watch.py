import importlib.util
import sys
import unittest
from pathlib import Path


TOOLS_PATH = Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(TOOLS_PATH))
MODULE_PATH = TOOLS_PATH / "tonex_state_watch.py"
SPEC = importlib.util.spec_from_file_location("tonex_state_watch", MODULE_PATH)
watch = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = watch
SPEC.loader.exec_module(watch)


class StateWatchTests(unittest.TestCase):
    def preset_payload(self, index):
        payload = bytearray(32)
        payload[:12] = bytes.fromhex("b9 03 81 04 02 81 00 04 10 b9 03 01")
        payload[12] = index
        payload[13 : 13 + len(watch.protocol.NAME_MARKER)] = watch.protocol.NAME_MARKER
        return bytes(payload)

    def test_state_sample_reports_code_and_stable_fingerprint(self):
        payload = bytes.fromhex("b9 03 81 01 02 03 10 b9 01 00")
        first = watch.state_sample(payload, 1.234)
        second = watch.state_sample(payload, 2.0)
        self.assertEqual(first["state_code"], 0)
        self.assertEqual(first["elapsed_ms"], 1234)
        self.assertEqual(first["payload_fingerprint"], second["payload_fingerprint"])

    def test_preset_sample_validates_expected_index_without_exposing_payload(self):
        sample = watch.preset_sample(7, self.preset_payload(7))
        self.assertEqual(sample["preset_index"], 7)
        self.assertEqual(sample["preset_payload_bytes"], 32)
        self.assertEqual(len(sample["preset_payload_fingerprint"]), 16)
        self.assertNotIn("payload_hex", sample)

        with self.assertRaisesRegex(watch.protocol.ProtocolError, "does not match"):
            watch.preset_sample(8, self.preset_payload(7))


if __name__ == "__main__":
    unittest.main()
