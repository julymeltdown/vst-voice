from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.evidence_archive import create_archive_manifest
from tools.external_beta.evidence_audit import audit_candidate

ROOT = Path(__file__).resolve().parents[2]


def _candidate(root: Path) -> tuple[dict, dict]:
    raw = root / "archive" / "record.json"
    raw.parent.mkdir(parents=True)
    raw.write_bytes(b"raw-record")
    digest = hashlib.sha256(raw.read_bytes()).hexdigest()
    manifest = create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001")
    candidate = {
        "schemaVersion": 1,
        "gate": "EXTERNAL_BETA_READY",
        "candidateRoot": {"id": "candidate-root-001", "status": "SEALED", "sha256": "a" * 64},
        "archive": {"locator": "archive-001", "sha256": manifest["manifestSha256"], "anchored": True, "immutable": True},
        "evidence": [{
            "recordId": "record-001",
            "candidateRootId": "candidate-root-001",
            "stageNodeId": "installed-macos-001",
            "parentEdgeId": "edge-001",
            "status": "PASS",
            "trustedTime": "2026-08-22T01:00:00Z",
            "roles": {"producer": "A3", "reviewer": "A6", "approver": "A3"},
            "rawArchive": {"locator": "archive/record.json", "sha256": digest},
        }],
    }
    return candidate, manifest


class EvidenceAuditTests(unittest.TestCase):
    def test_candidate_audit_passes_from_restored_archive_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            result = audit_candidate(candidate, manifest, root)
            self.assertTrue(result.passed, result.errors)

    def test_raw_archive_hash_or_role_mismatch_blocks_audit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            changed = copy.deepcopy(candidate)
            changed["evidence"][0]["rawArchive"]["sha256"] = "f" * 64
            changed["evidence"][0]["roles"]["reviewer"] = "A3"
            result = audit_candidate(changed, manifest, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("raw archive" in error.lower() for error in result.errors))
            self.assertTrue(any("independent" in error.lower() for error in result.errors))

    def test_cli_expect_blocked_accepts_incomplete_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            candidate["evidence"] = []
            candidate_path = root / "candidate.json"
            manifest_path = root / "manifest.json"
            candidate_path.write_text(json.dumps(candidate), encoding="utf-8")
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(ROOT / "scripts/run_external_beta_evidence_audit.py"), "--candidate", str(candidate_path), "--archive-manifest", str(manifest_path), "--archive-root", str(root), "--expect-blocked"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            self.assertIn('"passed": false', completed.stdout)


if __name__ == "__main__":
    unittest.main()
