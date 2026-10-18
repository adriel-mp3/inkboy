"""The compact log summary accepts deferred waits in old trace records."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "log_report", Path(__file__).resolve().parents[1] / "tools/log_report.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class LogReportTest(unittest.TestCase):
    def test_deferred_wait_trace(self):
        lines = (
            "profile event=summary status=0 emu_fps=55 normal_fps=1 refresh_failures=0",
            "display event=trace kind=normal wait_ms=deferred completion=deferred",
            "display event=trace kind=normal wait_ms=0.025 completion=complete",
            "runtime event=final status=0",
            "cleanup stage=restore result=0",
            "cleanup stage=restore_wait result=0",
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_text("\n".join(lines))
            result = report.read_log(path)
        self.assertEqual(result["normal_wait_ms"], 0.025)
        self.assertEqual(result["wait_mode"], "deferred")
        self.assertTrue(result["restored"])

    def test_legacy_deferred_wait_field(self):
        lines = (
            "profile event=summary status=0 emu_fps=55 normal_fps=1 refresh_failures=0",
            "display event=trace kind=normal wait_ms=deferred",
            "display event=completion kind=normal result=ok deferred=1 wait_ms=0.025",
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_text("\n".join(lines))
            result = report.read_log(path)
        self.assertEqual(result["wait_mode"], "deferred")
        self.assertNotIn("normal_wait_ms", result)


if __name__ == "__main__":
    unittest.main()
