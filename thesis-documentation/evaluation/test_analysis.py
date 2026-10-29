import json
from pathlib import Path
import tempfile
import unittest

from analyze_results import analyze, qt_metrics, stats


class AnalysisTest(unittest.TestCase):
    def test_empty_statistics_do_not_invent_zero(self):
        self.assertIsNone(stats([])["mean"])
        self.assertEqual(stats([1, 2, 3, 4])["p50"], 2.5)

    def test_qt_interval_and_reset(self):
        samples = [{"utc_ms": t * 1000, "elapsed_ns": t * 10**9, "preview_present": True,
                    "rendered_frames": frames, "bridge_dropped_frames": 0, "invalid_frames": 0, "interop_failures": 0}
                   for t, frames in ((1, 5), (3, 65))]
        self.assertEqual(qt_metrics(samples, 1000, 3000)["fps"], 30)
        samples[-1]["rendered_frames"] = 0
        self.assertIsNone(qt_metrics(samples, 1000, 3000)["fps"])
        self.assertIsNone(qt_metrics([], 1000, 3000)["fps"])

    def test_missing_resources_and_qt_are_not_zero(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            condition = {"name": "live-only-1", "recording": False, "utc_start_ms": 1000, "utc_end_ms": 3000,
                         "interval_s": 2, "presented_fps": 30, "received_rtp_mbps": 2}
            (directory / "benchmark-result.json").write_text(json.dumps({"status": "passed", "run_id": "test",
                "conditions": [condition], "recordings": []}))
            result = analyze(directory)
            self.assertIsNone(result["conditions"][0]["qt"]["fps"])
            self.assertIsNone(result["groups"]["live_only"]["total_cpu_percent"]["mean"])
            self.assertIsNone(result["comparisons"]["total_cpu_percent"]["percent_change"])
            self.assertTrue((directory / "comparison.csv").exists())

    def test_resources_use_only_complete_intervals(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            condition = {"name": "live-only-1", "recording": False, "utc_start_ms": 1000, "utc_end_ms": 3000,
                         "interval_s": 2, "presented_fps": 30, "received_rtp_mbps": 2}
            (directory / "benchmark-result.json").write_text(json.dumps({"status": "passed", "run_id": "test",
                "conditions": [condition], "recordings": []}))
            samples = [{"utc_ms": time, "valid_cpu_interval": True, "interval_s": 1,
                        "components": {"stream-edge": {"cpu_percent": cpu, "rss_bytes": 1024**2}}}
                       for time, cpu in ((1500, 100), (2500, 20))]
            (directory / "resources.jsonl").write_text("\n".join(map(json.dumps, samples)))
            result = analyze(directory)
            self.assertEqual(result["groups"]["live_only"]["total_cpu_percent"]["mean"], 20)


if __name__ == "__main__":
    unittest.main()
