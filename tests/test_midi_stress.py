import importlib.util
import sys
import unittest
from pathlib import Path


TOOLS_PATH = Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(TOOLS_PATH))
MODULE_PATH = TOOLS_PATH / "tonex_midi_stress.py"
SPEC = importlib.util.spec_from_file_location("tonex_midi_stress", MODULE_PATH)
stress = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = stress
SPEC.loader.exec_module(stress)


class MidiStressTests(unittest.TestCase):
    def test_deterministic_sequence_has_no_adjacent_repeats_and_covers_library(self):
        sequence = stress.deterministic_preset_sequence(149)
        self.assertEqual(len(set(sequence)), 149)
        self.assertEqual(set(sequence), set(range(1, 150)))
        self.assertTrue(all(left != right for left, right in zip(sequence, sequence[1:])))

    def test_parse_transmissions_includes_explicit_and_cleanup_restores(self):
        output = "\n".join(
            [
                "destination=TONEX MIDI Out",
                "sent index=1 display=0.B",
                "restored index=0 display=0.A",
                "cleanup_restore index=7 display=2.B",
            ]
        )
        self.assertEqual(stress.parse_transmitted_indices(output), [1, 0, 7])

    def test_exact_event_analysis_passes(self):
        analysis = stress.analyze_event_sequence([1, 38, 0], [1, 38, 0])
        self.assertTrue(analysis["exact_match"])
        self.assertEqual(analysis["missing_count"], 0)
        self.assertEqual(analysis["excess_count"], 0)
        self.assertIsNone(analysis["first_mismatch"])

    def test_event_analysis_reports_drop_and_excess(self):
        analysis = stress.analyze_event_sequence([1, 38, 75, 0], [1, 75, 75, 0])
        self.assertFalse(analysis["exact_match"])
        self.assertEqual(analysis["missing_index_counts"], {38: 1})
        self.assertEqual(analysis["excess_index_counts"], {75: 1})
        self.assertFalse(analysis["reordered"])
        self.assertEqual(
            analysis["first_mismatch"],
            {"position": 1, "expected": 38, "observed": 75},
        )

    def test_event_analysis_distinguishes_reordering(self):
        analysis = stress.analyze_event_sequence([1, 38, 75, 0], [1, 75, 38, 0])
        self.assertFalse(analysis["exact_match"])
        self.assertTrue(analysis["reordered"])
        self.assertEqual(analysis["missing_count"], 0)
        self.assertEqual(analysis["excess_count"], 0)


if __name__ == "__main__":
    unittest.main()
