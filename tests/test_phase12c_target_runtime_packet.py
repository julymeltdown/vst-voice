import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.verify_phase12c_target_runtime_packet import verify_packet
from tests.test_phase12c_target_runtime import passing_summary, sha256

ROOT = Path(__file__).resolve().parents[1]


class Phase12CTargetRuntimePacketTests(unittest.TestCase):
    def test_record_binds_each_packet_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "phase11-clap-editor-summary.json").write_text(
                json.dumps(passing_summary()), encoding="utf-8"
            )
            (root / "phase11-clap-editor.ppm").write_bytes(b"P6\n1 1\n255\n\0\0\0")
            (root / "phase11-clap-editor-live.wav").write_bytes(b"RIFF")
            (root / "phase11-clap-editor.png").write_bytes(b"PNG")
            (root / "phase11-clap-editor-host.log").write_text(
                "Phase 11 CLAP editor host: PASS\n", encoding="utf-8"
            )
            (root / "phase12c-runner.json").write_text(
                '{"runnerOs":"Darwin","runnerArchitecture":"arm64"}\n',
                encoding="utf-8",
            )
            record_path = root / "target-runtime-record.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/record_phase12c_target_runtime.py"),
                    "--platform", "macos", "--build-root", str(root),
                    "--derived-png", str(root / "phase11-clap-editor.png"),
                    "--host-log", str(root / "phase11-clap-editor-host.log"),
                    "--runner-metadata", str(root / "phase12c-runner.json"),
                    "--output", str(record_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            record = json.loads(record_path.read_text(encoding="utf-8"))
            self.assertEqual(record["runnerMetadata"]["path"], "phase12c-runner.json")
            self.assertEqual(verify_packet(record_path, root), [])

    def test_uploaded_packet_rejects_missing_hash_bound_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "phase11-clap-editor-summary.json").write_text(
                json.dumps(passing_summary()), encoding="utf-8"
            )
            (root / "phase11-clap-editor.ppm").write_bytes(b"P6")
            (root / "phase11-clap-editor-live.wav").write_bytes(b"RIFF")
            (root / "phase11-clap-editor-host.log").write_text(
                "Phase 11 CLAP editor host: PASS\n", encoding="utf-8"
            )
            (root / "phase12c-runner.json").write_text(
                '{"runnerOs":"Darwin","runnerArchitecture":"arm64"}\n',
                encoding="utf-8",
            )
            record_path = root / "target-runtime-record.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/record_phase12c_target_runtime.py"),
                    "--platform", "macos", "--build-root", str(root),
                    "--host-log", str(root / "phase11-clap-editor-host.log"),
                    "--runner-metadata", str(root / "phase12c-runner.json"),
                    "--output", str(record_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            (root / "phase11-clap-editor.ppm").unlink()
            self.assertTrue(verify_packet(record_path, root))

    def test_uploaded_packet_rejects_symlinked_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "real-summary.json"
            target.write_text(json.dumps(passing_summary()), encoding="utf-8")
            summary = root / "phase11-clap-editor-summary.json"
            try:
                summary.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            screenshot = root / "phase11-clap-editor.ppm"
            screenshot.write_bytes(b"P6")
            audio = root / "phase11-clap-editor-live.wav"
            audio.write_bytes(b"RIFF")
            host_log = root / "phase11-clap-editor-host.log"
            host_log.write_text("Phase 12C target runtime: PASS\n", encoding="utf-8")
            runner = root / "phase12c-runner.json"
            runner.write_text('{"runnerOs":"Darwin"}\n', encoding="utf-8")
            record = root / "runtime.json"
            record.write_text(
                json.dumps({
                    "schemaVersion": 1,
                    "recordType": "phase12c-target-runtime",
                    "platform": "macos",
                    "implementationState": "TARGET_BUILD_PASS",
                    "runtimeResult": "PASS",
                    "summary": {"path": summary.name, "sha256": sha256(target)},
                    "screenshot": {"path": screenshot.name, "sha256": sha256(screenshot)},
                    "audio": {"path": audio.name, "sha256": sha256(audio)},
                    "runnerMetadata": {"path": runner.name, "sha256": sha256(runner)},
                    "hostLog": {"path": host_log.name, "sha256": sha256(host_log)},
                }),
                encoding="utf-8",
            )
            errors = verify_packet(record, root)
            self.assertTrue(any("symbolic link" in error for error in errors))

    def test_uploaded_packet_rejects_non_pass_runtime_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts = {
                "summary": root / "summary.json",
                "screenshot": root / "screen.ppm",
                "audio": root / "audio.wav",
                "runnerMetadata": root / "runner.json",
                "hostLog": root / "host.log",
            }
            for path in artifacts.values():
                path.write_bytes(b"artifact")
            record = root / "runtime.json"
            record.write_text(
                json.dumps({
                    "schemaVersion": 1,
                    "recordType": "phase12c-target-runtime",
                    "platform": "macos",
                    "implementationState": "TARGET_BUILD_PASS",
                    "runtimeResult": "FAIL",
                    **{
                        key: {"path": path.name, "sha256": sha256(path)}
                        for key, path in artifacts.items()
                    },
                }),
                encoding="utf-8",
            )
            errors = verify_packet(record, root)
            self.assertIn("record runtimeResult must be PASS", errors)

    def test_uploaded_packet_rejects_malformed_runner_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts = {
                "summary": root / "summary.json",
                "screenshot": root / "screen.ppm",
                "audio": root / "audio.wav",
                "runnerMetadata": root / "runner.json",
                "hostLog": root / "host.log",
            }
            for path in artifacts.values():
                path.write_bytes(b"artifact")
            artifacts["runnerMetadata"].write_text('{}\n', encoding="utf-8")
            record = root / "runtime.json"
            record.write_text(
                json.dumps({
                    "schemaVersion": 1,
                    "recordType": "phase12c-target-runtime",
                    "platform": "macos",
                    "implementationState": "TARGET_BUILD_PASS",
                    "runtimeResult": "PASS",
                    **{
                        key: {"path": path.name, "sha256": sha256(path)}
                        for key, path in artifacts.items()
                    },
                }),
                encoding="utf-8",
            )
            errors = verify_packet(record, root)
            self.assertTrue(any("runner metadata" in error for error in errors))

    def test_workflow_packet_relocates_record_referenced_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_root = root / "build"
            packet_root = root / "packet"
            build_root.mkdir()
            packet_root.mkdir()
            (build_root / "phase11-clap-editor-summary.json").write_text(
                json.dumps(passing_summary()), encoding="utf-8"
            )
            (build_root / "phase11-clap-editor.ppm").write_bytes(b"P6")
            (build_root / "phase11-clap-editor-live.wav").write_bytes(b"RIFF")
            (build_root / "phase11-clap-editor-host.log").write_text(
                "Phase 11 CLAP editor host: PASS\n", encoding="utf-8"
            )
            (build_root / "phase12c-runner.json").write_text(
                '{"runnerOs":"Darwin","runnerArchitecture":"arm64"}\n',
                encoding="utf-8",
            )
            build_record = build_root / "runtime.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/record_phase12c_target_runtime.py"),
                    "--platform", "macos", "--build-root", str(build_root),
                    "--host-log", str(build_root / "phase11-clap-editor-host.log"),
                    "--runner-metadata", str(build_root / "phase12c-runner.json"),
                    "--output", str(build_record),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in (
                "phase11-clap-editor-summary.json",
                "phase11-clap-editor.ppm",
                "phase11-clap-editor-live.wav",
                "phase11-clap-editor-host.log",
                "phase12c-runner.json",
            ):
                shutil.copy2(build_root / name, packet_root / name)
            packet_record = packet_root / "runtime.json"
            shutil.copy2(build_record, packet_record)
            self.assertEqual(verify_packet(packet_record, packet_root), [])


if __name__ == "__main__":
    unittest.main()
