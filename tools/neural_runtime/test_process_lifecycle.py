import json
from pathlib import Path
import sys
import tempfile
import unittest

from tools.neural_runtime.check_process_lifecycle import run


class ProcessLifecycleTests(unittest.TestCase):
    def test_clean_exits_retain_logs(self):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "run"
            report = run([sys.executable, "-c", "print('inference checked')"], output, 2, 10)
            self.assertTrue(report["passed"])
            self.assertFalse(report["releaseEligible"])
            self.assertEqual(len(report["attempts"]), 2)
            self.assertEqual(json.loads((output / "report.json").read_text()), report)
            self.assertEqual((output / "001.stdout").read_text(), "inference checked\n")
            with self.assertRaises(FileExistsError):
                run([sys.executable], output, 1, 10)

    def test_success_json_does_not_hide_bad_exit(self):
        with tempfile.TemporaryDirectory() as root:
            report = run([sys.executable, "-c", "print('{\"passed\":true}'); raise SystemExit(7)"],
                         Path(root) / "run", 2, 10)
            self.assertFalse(report["passed"])
            self.assertEqual([a["exitCode"] for a in report["attempts"]], [7, 7])

    def test_timeout_and_launch_failure_are_not_success(self):
        with tempfile.TemporaryDirectory() as root:
            report = run([sys.executable, "-c", "import time; time.sleep(20)"],
                         Path(root) / "timeout", 1, .05)
            self.assertTrue(report["attempts"][0]["timedOut"])
            self.assertFalse(report["passed"])
            report = run([str(Path(root) / "missing")], Path(root) / "missing-run", 1, 10)
            self.assertIsNotNone(report["attempts"][0]["launchError"])
            self.assertFalse(report["passed"])

    def test_invalid_bounds_create_nothing(self):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "run"
            for command, attempts, timeout in (([], 1, 10), (["x"], 0, 10),
                                                (["x"], 101, 10), (["x"], 1, float("nan"))):
                with self.assertRaises(ValueError):
                    run(command, output, attempts, timeout)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
