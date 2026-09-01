import sys
import unittest
from pathlib import Path


TOOLS_PATH = Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(TOOLS_PATH))

import tonex_control_capture as capture


class ControlCaptureTests(unittest.TestCase):
    def test_raw_output_is_explicit_and_local_path_is_preserved(self):
        args = capture.parse_args(
            [
                "--label",
                "global_input_delta",
                "--raw-output",
                "diagnostics/global-input.json",
            ]
        )

        self.assertEqual(args.label, "global_input_delta")
        self.assertEqual(args.raw_output, Path("diagnostics/global-input.json"))

    def test_raw_output_defaults_to_disabled(self):
        self.assertIsNone(capture.parse_args([]).raw_output)


if __name__ == "__main__":
    unittest.main()
