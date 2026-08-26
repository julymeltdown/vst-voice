from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.evidence_archive import create_archive_manifest
from tools.external_beta.release_audit import audit_release

ROOT = Path(__file__).resolve().parents[2]


def _candidate(root: Path) -> tuple[dict, dict]:
    raw = root / "archive" / "cohort.json"
    raw.parent.mkdir(parents=True)
    raw.write_bytes(b"cohort")
    manifest = create_archive_manifest("candidate-root-001", root, ["archive/cohort.json"], "archive-001", anchor_locator="https://8.8.8.8/archive-001")
    return {
        "schemaVersion": 1,
        "gate": "EXTERNAL_BETA_CLOSED",
        "candidateRoot": {"id": "candidate-root-001", "status": "SEALED", "sha256": "a" * 64},
        "archive": {"locator": "archive-001", "sha256": manifest["manifestSha256"], "anchored": True, "immutable": True},
        "releaseIdentity": {"product": "Project SEAM", "version": "0.13.1", "buildId": "build-1", "sourceCommit": "b" * 40, "buildEpoch": 1},
        "evidence": [{"recordId": "record-1", "candidateRootId": "candidate-root-001", "stageNodeId": "stage-1", "parentEdgeId": "edge-1", "status": "PASS", "trustedTime": "2026-08-22T00:00:00Z", "roles": {"producer": "A3", "reviewer": "A6", "approver": "A3"}, "rawArchive": {"locator": "archive/cohort.json", "sha256": "b" * 64}}],
    }, manifest


class ReleaseAuditTests(unittest.TestCase):
    def test_combined_audit_rejects_missing_release_requirements(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            candidate, manifest = _candidate(Path(directory))
            result = audit_release(candidate, manifest, Path(directory), "CLOSED")
            self.assertFalse(result.passed)
            self.assertTrue(any("requirement" in error for error in result.errors))
            self.assertTrue(any("cohort" in error for error in result.errors))

    def test_combined_audit_surfaces_strict_cohort_errors(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            candidate["cohort"] = {"status": "PASS", "decision": "CLOSED", "assignments": [{"participantId": "participant-a", "platform": "macos", "status": "COMPLETED", "reason": "done", "email": "private@example.com"}]}
            result = audit_release(candidate, manifest, root, "CLOSED")
            self.assertFalse(result.passed)
            self.assertTrue(any("PII" in error for error in result.errors))

    def test_cli_expect_blocked_is_observable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            candidate_path = root / "candidate.json"
            manifest_path = root / "manifest.json"
            candidate_path.write_text(json.dumps(candidate), encoding="utf-8")
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(ROOT / "scripts/run_external_beta_release_audit.py"), "--candidate", str(candidate_path), "--archive-manifest", str(manifest_path), "--archive-root", str(root), "--state", "CLOSED", "--expect-blocked"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            self.assertIn('"passed": false', completed.stdout)


if __name__ == "__main__":
    unittest.main()
