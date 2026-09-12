from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.verify_phase12c_canonical_contract import validate_matrix, validate_soak, verify


ROOT = Path(__file__).resolve().parents[2]


class Phase12CCanonicalContractTests(unittest.TestCase):
    def matrix_report(self) -> dict:
        return {
            "result": "PASS", "cases": 336, "expected": 336,
            "failures": 0, "finite": True,
            "executionPath": "clap-plugin-process-v1", "resourceMode": "development-fixture",
            "pluginId": "com.project-seam.editor", "pluginSha256": "a" * 64,
            "voicebankId": "fixture-bank", "voicebankVersion": "1.0.0",
            "voicebankTreeSha256": "b" * 64,
            "sourceCommit": "c" * 40, "buildId": "release-test-build",
            "rowResults": [
                {
                    "sampleRate": rate, "blockFrames": frames, "channels": channels,
                    "dialect": dialect, "result": "PASS", "finite": True,
                    "processCalls": 16, "renderedFrames": rate // 10,
                    "absoluteEnergy": 1.0, "stateRoundTrip": True,
                    "preNoteSilent": True, "releaseSilent": True,
                    "channelCountVerified": True, "pitchBendChanged": True,
                    "panChannelIsolation": True, "eventAdmissionBounded": True,
                    "processingStatusVerified": True,
                }
                for rate in (44100, 48000, 88200, 96000, 176400, 192000)
                for frames in (16, 32, 64, 128, 256, 512, 1024)
                for channels in (1, 2, 4, 8)
                for dialect in ("clap", "midi1")
            ],
        }

    def soak_report(self) -> dict:
        report = self.matrix_report()
        for key in ("cases", "expected", "failures", "rowResults"):
            del report[key]
        report.update({
            "profile": "full", "elapsedSeconds": 7200.0,
            "eventBlocks": 1, "resourcePublishes": 1, "resourceClears": 1,
            "noteOns": 1, "noteOffs": 1, "steals": 1, "midiEvents": 1,
            "expressionEvents": 1, "renderedFrames": 44100 * 7200,
            "transitionHits": 1, "transitionFallbacks": 1,
            "eventOverflows": 0, "maxActiveVoices": 32,
        })
        return report

    def validate_report(self, report: dict, *, soak: bool = False) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            identity = {"plugin_sha256": "a" * 64, "bank": {"id": "fixture-bank", "version": "1.0.0", "treeSha256": "b" * 64}}
            if soak:
                return validate_soak(path, require_full=True, **identity)
            return validate_matrix(path, **identity)

    def test_source_contract_is_bound_to_the_canonical_editor(self) -> None:
        errors, result = verify(root=ROOT)
        self.assertEqual([], errors, result)
        self.assertEqual("PASS", result["status"])
        self.assertEqual("com.project-seam.editor", result["pluginId"])
        self.assertEqual("engineering", result["evidenceScope"])
        self.assertIs(False, result["releaseEligible"])

    def test_complete_plugin_matrix_is_accepted_for_both_resource_modes(self) -> None:
        for resource_mode in ("development-fixture", "installed-bank"):
            with self.subTest(resource_mode=resource_mode):
                report = self.matrix_report()
                report["resourceMode"] = resource_mode
                self.assertEqual([], self.validate_report(report))

    def test_old_hashed_plugin_summary_is_not_plugin_execution(self) -> None:
        report = self.matrix_report()
        for key in ("executionPath", "resourceMode", "rowResults"):
            del report[key]
        errors = self.validate_report(report)
        self.assertTrue(any("executionPath" in error for error in errors), errors)
        self.assertTrue(any("rowResults" in error for error in errors), errors)

    def test_matrix_requires_each_dimension_combination_once(self) -> None:
        report = self.matrix_report()
        report["rowResults"][-1] = report["rowResults"][0].copy()
        errors = self.validate_report(report)
        self.assertTrue(any("duplicates" in error for error in errors), errors)
        self.assertTrue(any("exactly once" in error for error in errors), errors)

    def test_matrix_requires_observed_processing_and_each_behavior(self) -> None:
        invalid_values = {
            "result": "FAIL", "finite": False, "stateRoundTrip": False,
            "preNoteSilent": False, "releaseSilent": False,
            "channelCountVerified": False, "pitchBendChanged": False,
            "panChannelIsolation": False, "eventAdmissionBounded": False,
            "processingStatusVerified": False,
            "processCalls": 0, "renderedFrames": 4409, "absoluteEnergy": 0,
        }
        for field, invalid in invalid_values.items():
            with self.subTest(field=field):
                report = self.matrix_report()
                report["rowResults"][0][field] = invalid
                errors = self.validate_report(report)
                self.assertTrue(any(field in error for error in errors), errors)

    def test_matrix_rejects_malformed_numeric_and_dimension_fields(self) -> None:
        for field, invalid in (
            ("sampleRate", True), ("sampleRate", "44100"), ("channels", []),
            ("dialect", {}), ("blockFrames", 16.0), ("processCalls", True),
            ("processCalls", 1.0), ("renderedFrames", True),
            ("absoluteEnergy", True), ("absoluteEnergy", "1"),
            ("absoluteEnergy", float("nan")), ("absoluteEnergy", float("inf")),
        ):
            with self.subTest(field=field, invalid=invalid):
                report = self.matrix_report()
                report["rowResults"][0][field] = invalid
                self.assertTrue(self.validate_report(report))
        report = self.matrix_report()
        report["failures"] = False
        self.assertTrue(self.validate_report(report))

    def test_nonfinite_exponent_is_rejected_before_matrix_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.json"
            path.write_text(json.dumps(self.matrix_report()).replace('"absoluteEnergy": 1.0', '"absoluteEnergy": 1e999'), encoding="utf-8")
            errors = validate_matrix(path, plugin_sha256="a" * 64, bank={})
            self.assertTrue(any("non-finite" in error for error in errors), errors)

    def test_matrix_rejects_placeholder_build_identity(self) -> None:
        for field, invalid in (
            ("sourceCommit", "0" * 40), ("sourceCommit", "0" * 64),
            ("sourceCommit", "abc123"), ("sourceCommit", True),
            ("buildId", "0" * 40), ("buildId", 0),
            ("buildId", "unknown"), ("buildId", " "),
            ("executionPath", "linked-voice-engine-v1"),
            ("resourceMode", "production-qualified"),
        ):
            with self.subTest(field=field, invalid=invalid):
                report = self.matrix_report()
                report[field] = invalid
                errors = self.validate_report(report)
                self.assertTrue(any(field in error for error in errors), errors)

    def test_soak_rejects_linked_engine_execution(self) -> None:
        report = self.soak_report()
        self.assertEqual([], self.validate_report(report, soak=True))
        del report["executionPath"]
        errors = self.validate_report(report, soak=True)
        self.assertTrue(any("executionPath" in error for error in errors), errors)

    def test_soak_requires_exact_and_fallback_transitions(self) -> None:
        for field in ("transitionHits", "transitionFallbacks"):
            for invalid in (0, -1, True):
                with self.subTest(field=field, invalid=invalid):
                    report = self.soak_report()
                    report[field] = invalid
                    errors = self.validate_report(report, soak=True)
                    self.assertTrue(any(field in error for error in errors), errors)

    def test_soak_rejects_invalid_counters_and_elapsed_time(self) -> None:
        for field, invalid in (
            ("eventBlocks", True), ("eventOverflows", False),
            ("maxActiveVoices", True), ("maxActiveVoices", "32"),
            ("maxActiveVoices", 33), ("elapsedSeconds", True),
            ("elapsedSeconds", "7200"), ("elapsedSeconds", 7199.9),
            ("elapsedSeconds", float("nan")), ("elapsedSeconds", float("inf")),
        ):
            with self.subTest(field=field, invalid=invalid):
                report = self.soak_report()
                report[field] = invalid
                self.assertTrue(self.validate_report(report, soak=True))

    def test_require_full_soak_cannot_pass_without_soak_evidence(self) -> None:
        errors, result = verify(root=ROOT, require_full_soak=True)
        self.assertTrue(any("requires a soak evidence file" in error for error in errors), errors)
        self.assertEqual("BLOCKED", result["status"])
        self.assertIs(False, result["releaseEligible"])

    def test_matrix_requires_plugin_and_bank_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin = root / "ProjectSEAMEditor.clap"
            plugin.write_bytes(b"canonical-plugin")
            bank = root / "bank"
            bank.mkdir()
            (bank / "manifest.json").write_text(
                json.dumps({"schemaVersion": 3, "id": "fixture-bank", "version": "1.0.0", "units": [{"id": "unit"}]}),
                encoding="utf-8",
            )
            matrix = root / "matrix.json"
            matrix.write_text(
                json.dumps({
                    "result": "PASS", "cases": 336, "expected": 336,
                    "failures": 0, "finite": True,
                    "pluginId": "com.project-seam.editor",
                    "pluginSha256": hashlib.sha256(plugin.read_bytes()).hexdigest(),
                    "voicebankId": "fixture-bank", "voicebankVersion": "1.0.0",
                    "voicebankTreeSha256": "0" * 64,
                    "sourceCommit": "a" * 40, "buildId": "fixture-build",
                }),
                encoding="utf-8",
            )
            errors, _ = verify(root=ROOT, plugin=plugin, bank=bank, matrix=matrix)
            self.assertTrue(any("voicebank tree digest" in error for error in errors), errors)

    def test_symlinked_plugin_does_not_get_resolved_by_cli(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "real.clap"
            target.write_bytes(b"plugin")
            linked = root / "linked.clap"
            try:
                linked.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/verify_phase12c_canonical_contract.py"),
                    "--root", str(ROOT), "--plugin", str(linked),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(3, result.returncode, result.stdout + result.stderr)
            self.assertIn("missing or linked", result.stdout)

    def test_duplicate_matrix_keys_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin = root / "ProjectSEAMEditor.clap"
            plugin.write_bytes(b"plugin")
            bank = root / "bank"
            bank.mkdir()
            (bank / "manifest.json").write_text(
                json.dumps({"schemaVersion": 3, "id": "bank", "version": "1", "units": [{"id": "unit"}]}),
                encoding="utf-8",
            )
            matrix = root / "matrix.json"
            matrix.write_text('{"result":"PASS","result":"PASS"}', encoding="utf-8")
            errors, _ = verify(root=ROOT, plugin=plugin, bank=bank, matrix=matrix)
            self.assertTrue(any("duplicate JSON key" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
