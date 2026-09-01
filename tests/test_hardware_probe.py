import importlib.util
import struct
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "tools" / "tonex_hardware_probe.py"
SPEC = importlib.util.spec_from_file_location("tonex_hardware_probe", MODULE_PATH)
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)


class HardwareProbeProtocolTests(unittest.TestCase):
    def preset_payload(self, index, length=None, unsolicited=False):
        extended = index >= 128
        payload = bytearray(length or (1190 if extended else 1189))
        payload[:12] = bytes.fromhex("b9 03 81 04 02 81 00 04 10 b9 03 01")
        payload[6] = 0x9D if extended else 0x9C
        if unsolicited:
            payload[7:12] = probe.ACTIVE_PRESET_EVENT_PREFIX
        if extended:
            payload[12:14] = bytes([0x80, index])
            name_marker_offset = 14
        else:
            payload[12] = index
            name_marker_offset = 13
        payload[name_marker_offset : name_marker_offset + len(probe.NAME_MARKER)] = probe.NAME_MARKER
        return payload

    def test_frame_round_trip_and_crc_rejection(self):
        payload = bytes([0x10, 0x7D, 0x7E, 0x20])
        framed = probe.build_frame(payload)
        self.assertEqual(probe.deframe(framed), payload)

        corrupted = bytearray(framed)
        corrupted[-2] ^= 0x01
        with self.assertRaises(probe.ProtocolError):
            probe.deframe(bytes(corrupted))

    def test_stream_decoder_reassembles_every_byte_fragmentation(self):
        payload = bytes([0x10, 0x7D, 0x7E, 0x20])
        decoder = probe.HdlcStreamDecoder()
        decoded = []
        for byte in probe.build_frame(payload):
            decoded.extend(decoder.feed(bytes([byte])))
        self.assertEqual(decoded, [payload])

    def test_stream_decoder_preserves_back_to_back_frames(self):
        first = b"first"
        second = b"second"
        decoder = probe.HdlcStreamDecoder()
        decoded = decoder.feed(probe.build_frame(first) + probe.build_frame(second))
        self.assertEqual(decoded, [first, second])

    def test_stream_decoder_recovers_after_corrupt_frame(self):
        corrupt = bytearray(probe.build_frame(b"corrupt"))
        corrupt[-2] ^= 0x01
        decoder = probe.HdlcStreamDecoder()
        decoded = decoder.feed(bytes(corrupt) + probe.build_frame(b"valid"))
        self.assertEqual(decoded, [b"valid"])
        self.assertIsInstance(decoder.last_protocol_error, probe.ProtocolError)
        self.assertEqual(decoder.protocol_error_count, 1)

    def test_stream_decoder_discards_oversized_partial_frame(self):
        decoder = probe.HdlcStreamDecoder(max_frame_bytes=8)
        self.assertEqual(decoder.feed(b"\x7e" + b"x" * 9), [])
        self.assertEqual(decoder.feed(probe.build_frame(b"valid")), [b"valid"])

    def test_unsolicited_payload_summary_limits_long_payload_data(self):
        short = probe.summarize_unsolicited_payload(bytes.fromhex("b9 01 00"))
        self.assertEqual(short["payload_bytes"], 3)
        self.assertEqual(short["event_type"], "unknown")
        self.assertEqual(short["payload_hex"], "b9 01 00")
        self.assertEqual(len(short["payload_fingerprint"]), 16)

        long_payload = bytes(range(100))
        long = probe.summarize_unsolicited_payload(long_payload)
        self.assertEqual(long["payload_bytes"], 100)
        self.assertEqual(long["payload_prefix_hex"], bytes(range(12)).hex(" "))
        self.assertNotIn("payload_hex", long)

        preset = probe.summarize_unsolicited_payload(bytes(self.preset_payload(149)))
        self.assertEqual(preset["event_type"], "solicited_preset")
        self.assertEqual(preset["preset_index"], 149)

        active = probe.summarize_unsolicited_payload(
            bytes(self.preset_payload(149, unsolicited=True))
        )
        self.assertEqual(active["event_type"], "active_preset")
        self.assertEqual(active["preset_index"], 149)

    def test_preset_request_boundary_encoding(self):
        preset_127 = probe.create_preset_request(127)
        preset_128 = probe.create_preset_request(128)
        self.assertEqual(len(preset_127), 17)
        self.assertEqual(preset_127[-2:], bytes([127, 0]))
        self.assertEqual(len(preset_128), 18)
        self.assertEqual(preset_128[-3:], bytes([0x80, 128, 0]))

    def test_parses_live_preset_response_index_encoding_boundaries(self):
        for index in (0, 127, 128, 149):
            with self.subTest(index=index):
                self.assertEqual(
                    probe.parse_preset_index(bytes(self.preset_payload(index))),
                    index,
                )

    def test_parses_unsolicited_active_preset_event_index(self):
        payload = bytes(self.preset_payload(149, unsolicited=True))
        self.assertEqual(probe.parse_active_preset_event_index(payload), 149)
        with self.assertRaisesRegex(probe.ProtocolError, "solicited"):
            probe.parse_preset_response_index(payload)

    def test_parses_short_state_response(self):
        payload = bytes.fromhex("b9 03 81 01 02 03 10 b9 01 00")
        self.assertEqual(probe.parse_state_response(payload), 0)
        self.assertEqual(probe.parse_state_response(payload[:-1] + b"\x7f"), 0x7F)
        with self.assertRaisesRegex(probe.ProtocolError, "State response"):
            probe.parse_state_response(payload[:-1])
        with self.assertRaisesRegex(probe.ProtocolError, "State response"):
            probe.parse_state_response(b"\x00" + payload[1:])

    def test_transport_demultiplexes_active_event_before_response(self):
        event = bytes(self.preset_payload(149, unsolicited=True))
        response = bytes(self.preset_payload(42))
        frames = iter([event, response])
        transport = probe.PosixSerialTransport("unused", timeout=1.0)
        transport._read_frame = lambda label, timeout=None: next(frames)
        active_events = []

        result = transport._read_matching_frame(
            "preset 42",
            lambda payload: probe.parse_preset_response_index(payload) == 42,
            active_events.append,
        )
        self.assertEqual(result, response)
        self.assertEqual(active_events, [149])

    def test_rejects_mismatched_preset_response_index(self):
        payload = self.preset_payload(128)
        name_offset = 14 + len(probe.NAME_MARKER)
        payload[name_offset : name_offset + 5] = b"Test\0"
        parameter_offset = 60
        payload[parameter_offset : parameter_offset + len(probe.PARAM_MARKER)] = probe.PARAM_MARKER
        with self.assertRaisesRegex(probe.ProtocolError, "does not match"):
            probe.parse_preset_response(127, bytes(payload))

    def test_parses_sanitized_live_response_shape(self):
        payload = self.preset_payload(0, length=1190)
        name_offset = 13 + len(probe.NAME_MARKER)
        payload[name_offset : name_offset + 12] = b"Test Preset\0"

        parameter_marker_offset = 60
        payload[
            parameter_marker_offset : parameter_marker_offset + len(probe.PARAM_MARKER)
        ] = probe.PARAM_MARKER
        parameter_offset = parameter_marker_offset + len(probe.PARAM_MARKER)
        amp_offset = parameter_offset + probe.AMP_ENABLE_INDEX * probe.FLOAT_SIZE
        cab_offset = parameter_offset + probe.CAB_TYPE_INDEX * probe.FLOAT_SIZE
        payload[amp_offset] = 0x88
        payload[amp_offset + 1 : amp_offset + 5] = struct.pack("<f", 1.0)
        payload[cab_offset] = 0x88
        payload[cab_offset + 1 : cab_offset + 5] = struct.pack("<f", 0.0)

        result = probe.parse_preset_response(0, bytes(payload))
        self.assertTrue(result.name_present)
        self.assertTrue(result.amp)
        self.assertTrue(result.cab)
        self.assertEqual(result.cab_type, "tone_model")
        self.assertIsNone(result.name)

        payload[cab_offset + 1 : cab_offset + 5] = struct.pack("<f", 2.0)
        result = probe.parse_preset_response(0, bytes(payload))
        self.assertFalse(result.cab)
        self.assertEqual(result.cab_type, "disabled")

    def test_rejects_obsolete_parameter_marker(self):
        payload = self.preset_payload(0, length=200)
        name_offset = 13 + len(probe.NAME_MARKER)
        payload[name_offset : name_offset + 5] = b"Test\0"
        payload[60:64] = bytes.fromhex("ba 03 ba 6d")
        with self.assertRaisesRegex(probe.ProtocolError, "parameter marker"):
            probe.parse_preset_response(0, bytes(payload))

    def test_summarizes_a_name_free_soak_cycle(self):
        result = {
            "hello_payload_bytes": 52,
            "state_payload_bytes": 10,
            "presets_checked": 2,
            "presets": [
                {
                    "index": 0,
                    "payload_bytes": 1189,
                    "name_present": True,
                    "amp": True,
                    "cab": True,
                    "cab_type": "tone_model",
                },
                {
                    "index": 128,
                    "payload_bytes": 1190,
                    "name_present": True,
                    "amp": True,
                    "cab": False,
                    "cab_type": "disabled",
                },
            ],
        }
        summary = probe.summarize_cycle(3, 1.234, result)
        self.assertEqual(summary["cycle"], 3)
        self.assertEqual(summary["duration_ms"], 1234)
        self.assertEqual(summary["payload_byte_counts"], {1189: 1, 1190: 1})
        self.assertEqual(summary["amp_enabled"], 2)
        self.assertEqual(summary["cab_enabled"], 1)
        self.assertEqual(summary["cab_type_counts"], {"disabled": 1, "tone_model": 1})


if __name__ == "__main__":
    unittest.main()
