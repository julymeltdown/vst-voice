import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class DependencyAcquisitionTests(unittest.TestCase):
    def _write_checkout(self, root: Path, name: str, commit: str) -> None:
        checkout = root / name
        checkout.mkdir(parents=True)
        (checkout / ".phase13a-revision").write_text(commit + "\n", encoding="utf-8")
        (checkout / "LICENSE").write_text("permissive license\n", encoding="utf-8")

    def test_verify_only_accepts_exact_non_git_checkouts(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            checkout_root = base / "deps"
            lock = json.loads((ROOT / "phase13a" / "dependency-lock.json").read_text())
            for dependency in lock["dependencies"]:
                self._write_checkout(checkout_root, dependency["name"], dependency["commit"])
            completed = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "fetch_phase13a_dependencies.py"),
                    "--output",
                    str(checkout_root),
                    "--verify-only",
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            self.assertIn("PHASE13A_DEPENDENCY_CHECKOUTS=PASS", completed.stdout)

    def test_verify_only_fails_when_a_locked_checkout_is_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            completed = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "fetch_phase13a_dependencies.py"),
                    "--output",
                    str(Path(directory) / "deps"),
                    "--verify-only",
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertNotEqual(0, completed.returncode)
            self.assertIn("checkout directory does not exist", completed.stderr)


if __name__ == "__main__":
    unittest.main()
