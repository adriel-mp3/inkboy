"""The report counts completions, separates cleanup waits, and rejects lost samples."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "task010_report", Path(__file__).resolve().parents[1] / "tools/task010_report.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class ReportTest(unittest.TestCase):
    def test_completions(self):
        metrics = dict.fromkeys((
            "initial_refresh_failures normal_refresh_failures cleanup_refresh_failures "
            "input_gap_max_ms input_gap_clean_max_ms emulation_avg_ms cpu_pct rss_kib "
            "peak_rss_kib unchanged_images refresh_failures emu_fps normal_fps cleanup_fps"
        ).split(), 0)
        metrics.update(runs=120, elapsed_s=60, presentations=0, initial_presentations=0,
                       normal_presentations=0, cleanups=0, refresh_attempts=0,
                       initial_wait_last_ms=360, refresh_wait_last_ms=100,
                       clean_wait_last_ms=370)
        lines = []

        def emit(event):
            lines.append("profile phase=gameplay event=" + event + " " + " ".join(
                f"{key}={value}" for key, value in metrics.items()))

        metrics.update(presentations=1, initial_presentations=1, refresh_attempts=1)
        emit("presentation")
        metrics.update(presentations=2, normal_presentations=1, refresh_attempts=2)
        emit("presentation")
        metrics.update(presentations=3, cleanups=1, refresh_attempts=3)
        emit("cleanup")
        # A failed attempt must not become a completed update or p95 sample.
        metrics.update(refresh_attempts=4, refresh_failures=1, refresh_wait_last_ms=999)
        emit("summary")
        lines.extend(("cleanup stage=restore result=0 duration_ms=1",
                      "cleanup stage=restore_wait result=0 duration_ms=370",
                      "runtime event=final status=1"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_text("\n".join(lines))
            result = report.summarize(path)
            self.assertEqual(result["normal_wait_p95_ms"], 100)
            self.assertEqual(result["cleanup_wait_p95_ms"], 370)
            self.assertEqual(result["normal_fps"], 1 / 60)
            self.assertEqual(result["emu_fps"], 2)
            self.assertTrue(result["restored"])
            self.assertEqual(result["final_status"], 1)
            path.write_text("\n".join(lines[:1] + lines[2:]))
            with self.assertRaises(ValueError):
                report.summarize(path)
            path.write_text("\n".join(lines[:-1]))
            with self.assertRaises(ValueError):
                report.summarize(path)


if __name__ == "__main__":
    unittest.main()
