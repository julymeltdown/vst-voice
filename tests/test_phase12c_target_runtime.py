import hashlib
import tempfile
import unittest
from pathlib import Path

from scripts.record_phase12c_target_runtime import validate_summary

ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def passing_summary() -> dict[str, str | int | float | bool]:
    return {
        "pluginId": "com.project-seam.editor",
        "hostApi": "cocoa",
        "fixtureId": "demo.public-domain.human.production",
        "fixtureVersion": "0.12.0",
        "fixtureContentHash": "0" * 64,
        "result": "PASS",
        "guiCreated": True, "guiVisible": True,
        "screenshotWritten": True, "audioWritten": True,
        "offlineRenderAccepted": True, "activeLoadRejected": True,
        "inactiveGuiLoadAccepted": True, "stateRoundTrip": True,
        "noteInputEnergy": 12.0,
        "capturedFrames": 24576,
        "outputChannels": 4,
        "restartRequests": 1,
        "processRequests": 3,
        "stateBytes": 128,
        "restoredStateBytes": 128,
        "stateSha256": "1" * 64,
        "restoredStateSha256": "1" * 64,
        "stateBytesEqual": True,
    }


class Phase12CTargetRuntimeTests(unittest.TestCase):
    def test_target_workflow_executes_and_records_runtime_evidence(self) -> None:
        workflow = (ROOT / ".github/workflows/phase12c-target-runtime.yml").read_text()
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("SEAM_RUN_NATIVE_GUI_TESTS=ON", workflow)
        self.assertIn("record_phase12c_target_runtime.py", workflow)
        self.assertIn("verify_phase12c_target_runtime_packet.py", workflow)
        self.assertIn("cp build/dev/phase11-clap-editor.ppm", workflow)
        self.assertIn("phase12c-runner.json", workflow)
        self.assertIn("--runner-metadata", workflow)
        self.assertIn("ctest --test-dir build/dev", workflow)
        self.assertNotIn('"runtimeResult":"NOT_RUN"', workflow)
        for marker in (
            "platform_host_appkit.mm",
            "platform_host_win32.cpp",
            "platform_host_x11.cpp",
            "--target-runtime-fixture-root",
        ):
            self.assertIn(marker, cmake)

    def test_passing_summary_matches_target_runtime_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            errors = validate_summary(passing_summary(), Path(directory), "macos")
        self.assertEqual(errors, [])

    def test_failed_gui_state_is_rejected(self) -> None:
        summary = passing_summary()
        summary["guiVisible"] = False
        with tempfile.TemporaryDirectory() as directory:
            errors = validate_summary(summary, Path(directory), "windows")
        self.assertIn("summary guiVisible is not true", errors)

    def test_host_api_must_match_declared_platform(self) -> None:
        summary = passing_summary()
        with tempfile.TemporaryDirectory() as directory:
            errors = validate_summary(summary, Path(directory), "windows")
        self.assertIn("summary hostApi 'cocoa' does not match windows", errors)

if __name__ == "__main__":
    unittest.main()
