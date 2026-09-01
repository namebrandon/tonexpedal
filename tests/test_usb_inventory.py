import importlib.util
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
MODULE_PATH = ROOT / "tools" / "tonex_usb_inventory.py"
SPEC = importlib.util.spec_from_file_location("tonex_usb_inventory", MODULE_PATH)
inventory_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = inventory_tool
SPEC.loader.exec_module(inventory_tool)


class UsbInventoryTests(unittest.TestCase):
    def test_role_inference_distinguishes_audio_and_midi_control(self):
        audio = {
            "bInterfaceClass": 1,
            "bInterfaceSubClass": 1,
            "kUSBString": "ToneX",
        }
        midi = {
            "bInterfaceClass": 1,
            "bInterfaceSubClass": 1,
            "kUSBString": "ToneX MIDI Control",
        }
        self.assertEqual(inventory_tool.infer_role(audio), "audio_control")
        self.assertEqual(inventory_tool.infer_role(midi), "midi_control")

    def test_sanitized_hardware_fixture_has_expected_interface_roles(self):
        fixture_path = ROOT / "tests" / "fixtures" / "tonex_usb_inventory.macos.json"
        inventory = json.loads(fixture_path.read_text())
        self.assertEqual(inventory["device"]["vendor_id"], 0x1963)
        self.assertEqual(inventory["device"]["product_id"], 0x0068)
        self.assertFalse(inventory["endpoint_details_available"])
        self.assertEqual(len(inventory["interfaces"]), 7)
        roles = {interface["role"] for interface in inventory["interfaces"]}
        self.assertTrue({"cdc_control", "cdc_data", "midi_streaming"}.issubset(roles))
        midi = next(interface for interface in inventory["interfaces"] if interface["role"] == "midi_streaming")
        self.assertEqual(midi["number"], 6)
        self.assertEqual(midi["endpoint_count"], 2)

    def test_inventory_omits_device_identifiers(self):
        fixture_path = ROOT / "tests" / "fixtures" / "tonex_usb_inventory.macos.json"
        serialized = fixture_path.read_text().lower()
        self.assertNotIn("serial_number", serialized)
        self.assertNotIn("locationid", serialized)
        self.assertNotIn("sessionid", serialized)


if __name__ == "__main__":
    unittest.main()
