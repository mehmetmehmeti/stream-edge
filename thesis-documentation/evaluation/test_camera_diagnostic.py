import unittest
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
from camera_diagnostic import frame_metrics, controls
from compare_camera_rate import compare


class CameraDiagnosticTest(unittest.TestCase):
    def test_driver_timestamps_and_warmup(self):
        log = "\n".join(f"cap dqbuf: 0 seq: {i} bytesused: 614400 ts: {10 + i / 30:.9f} field: None"
                        for i in range(121))
        result = frame_metrics(log, 1)
        self.assertEqual(result["measured_buffers"], 91)
        self.assertAlmostEqual(result["observed_fps"], 30)
        self.assertEqual(result["driver_sequence_gaps"], 0)

    def test_missing_or_reset_timestamps_rejected(self):
        with self.assertRaises(ValueError):
            frame_metrics("no buffers", 0)
        with self.assertRaises(ValueError):
            frame_metrics("cap dqbuf: 0 seq: 0 ts: 10\ncap dqbuf: 1 seq: 1 ts: 10", 0)

    def test_sequence_gaps(self):
        result = frame_metrics("cap dqbuf: 0 seq: 4 ts: 10\ncap dqbuf: 1 seq: 6 ts: 10.1", 0)
        self.assertAlmostEqual(result["observed_fps"], 10)
        self.assertEqual(result["driver_sequence_gaps"], 1)

    def test_error_buffers_are_not_video_frames(self):
        log = "cap dqbuf: 0 seq: 0 bytesused: 65536 ts: 10 field: None (error, ts-monotonic)\n" \
              "cap dqbuf: 1 seq: 1 bytesused: 776 ts: 11 field: None (error, ts-monotonic)"
        with self.assertRaisesRegex(ValueError, "Insufficient valid frames"):
            frame_metrics(log, 0, expected_bytes=614400)

    def test_only_complete_valid_buffers_count(self):
        log = "\n".join([
            "cap dqbuf: 0 seq: 0 bytesused: 614400 ts: 10.0 field: None (error, ts-monotonic)",
            "cap dqbuf: 1 seq: 1 bytesused: 614400 ts: 10.1 field: None (ts-monotonic)",
            "cap dqbuf: 2 seq: 2 bytesused: 65536 ts: 10.2 field: None (ts-monotonic)",
            "cap dqbuf: 3 seq: 3 bytesused: 614400 ts: 10.3 field: None (ts-monotonic)"])
        result = frame_metrics(log, 0, expected_bytes=614400)
        self.assertEqual(result["measured_buffers"], 2)
        self.assertEqual(result["driver_error_buffers"], 1)
        self.assertEqual(result["incomplete_buffers"], 1)
        self.assertAlmostEqual(result["observed_fps"], 5)

    def test_control_values(self):
        log = "auto_exposure 0x1 (menu): default=3 value=3\n exposure_dynamic_framerate 0x2 (bool): default=0 value=1"
        self.assertEqual(controls(log), {"auto_exposure": 3, "exposure_dynamic_framerate": 1})

    def test_control_restored_after_capture_failure(self):
        setting = "exposure_dynamic_framerate 0x2 (bool): default=0 value=1"
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(device="test-device", seconds=40, output=Path(directory) / "run")
            with patch("compare_camera_rate.settings", return_value=setting), \
                 patch("compare_camera_rate.subprocess.run") as run, \
                 patch("compare_camera_rate.capture", side_effect=RuntimeError("capture failed")):
                with self.assertRaisesRegex(RuntimeError, "capture failed"):
                    compare(args)
                self.assertEqual(run.call_args_list[0].args[0][-1], "exposure_dynamic_framerate=0")
                self.assertEqual(run.call_args_list[1].args[0][-1], "exposure_dynamic_framerate=1")
                self.assertEqual(json.loads((args.output / "restoration.json").read_text())["restored"], 1)

    def test_comparison_remeasures_after_restore(self):
        setting = "exposure_dynamic_framerate 0x2 (bool): default=0 value=1"
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(device="test-device", seconds=40, output=Path(directory) / "run")
            with patch("compare_camera_rate.settings", return_value=setting), \
                 patch("compare_camera_rate.subprocess.run"), \
                 patch("compare_camera_rate.capture") as capture:
                compare(args)
                self.assertEqual(capture.call_count, 2)
                self.assertEqual(capture.call_args_list[-1].args[0].label,
                                 "direct-v4l2-original-control-restored")


if __name__ == "__main__":
    unittest.main()
