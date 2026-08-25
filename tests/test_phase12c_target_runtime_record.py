import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.record_phase12c_target_runtime import evidence
from tests.test_phase12c_target_runtime import passing_summary

ROOT = Path(__file__).resolve().parents[1]


class Phase12CTargetRuntimeRecordTests(unittest.TestCase):
    def test_record_rejects_symlinked_source_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "actual-summary.json"
            target.write_text('{"result":"PASS"}\n', encoding="utf-8")
            link = root / "phase11-clap-editor-summary.json"
            try:
                link.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")

            with self.assertRaisesRegex(ValueError, "symbolic link"):
                evidence(link, root)

    def test_record_binds_runner_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "phase11-clap-editor-summary.json").write_text(
                json.dumps(passing_summary()), encoding="utf-8"
            )
            (root / "phase11-clap-editor.ppm").write_bytes(b"P6")
            (root / "phase11-clap-editor-live.wav").write_bytes(b"RIFF")
            (root / "phase11-clap-editor-host.log").write_text("PASS\n", encoding="utf-8")
            (root / "phase12c-runner.json").write_text(
                '{"runnerOs":"Darwin","runnerArchitecture":"arm64"}\n',
                encoding="utf-8",
            )
            output = root / "runtime.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/record_phase12c_target_runtime.py"),
                    "--platform", "macos", "--build-root", str(root),
                    "--host-log", str(root / "phase11-clap-editor-host.log"),
                    "--runner-metadata", str(root / "phase12c-runner.json"),
                    "--output", str(output),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            record = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(record["runnerMetadata"]["path"], "phase12c-runner.json")

    def test_record_rejects_malformed_runner_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "phase11-clap-editor-summary.json").write_text(
                json.dumps(passing_summary()), encoding="utf-8"
            )
            (root / "phase11-clap-editor.ppm").write_bytes(b"P6")
            (root / "phase11-clap-editor-live.wav").write_bytes(b"RIFF")
            (root / "phase11-clap-editor-host.log").write_text("PASS\n", encoding="utf-8")
            (root / "phase12c-runner.json").write_text('{}\n', encoding="utf-8")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/record_phase12c_target_runtime.py"),
                    "--platform", "macos", "--build-root", str(root),
                    "--host-log", str(root / "phase11-clap-editor-host.log"),
                    "--runner-metadata", str(root / "phase12c-runner.json"),
                    "--output", str(root / "runtime.json"),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("runner metadata", result.stderr)


if __name__ == "__main__":
    unittest.main()
