from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.external_beta.evidence_archive import create_archive_manifest
from tools.external_beta.evidence_audit import audit_candidate

ROOT = Path(__file__).resolve().parents[2]


def _candidate(root: Path) -> tuple[dict, dict]:
    raw = root / "archive" / "record.json"
    raw.parent.mkdir(parents=True)
    raw.write_bytes(b"raw-record")
    digest = hashlib.sha256(raw.read_bytes()).hexdigest()
    manifest = create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="https://evidence.example/archive-001")
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
    def test_candidate_audit_rejects_opaque_archived_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            with patch.dict("os.environ", {"SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256": manifest["anchor"]["sha256"]}):
                result = audit_candidate(candidate, manifest, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("serialized" in error.lower() for error in result.errors))

    def test_candidate_audit_accepts_bound_serialized_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, _ = _candidate(root)
            record = candidate["evidence"][0]
            raw = root / "archive" / "record.json"
            raw.write_text(
                json.dumps(
                    {key: value for key, value in record.items() if key != "rawArchive"},
                    sort_keys=True,
                ),
                encoding="utf-8",
            )
            manifest = create_archive_manifest(
                "candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="https://evidence.example/archive-001"
            )
            candidate["archive"]["sha256"] = manifest["manifestSha256"]
            record["rawArchive"]["sha256"] = manifest["entries"][0]["sha256"]

            with patch.dict("os.environ", {"SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256": manifest["anchor"]["sha256"]}):
                result = audit_candidate(candidate, manifest, root)

            self.assertTrue(result.passed, result.errors)

    def test_candidate_audit_rejects_archived_provenance_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, _ = _candidate(root)
            record = candidate["evidence"][0]
            raw = root / "archive" / "record.json"
            archived = {key: value for key, value in record.items() if key != "rawArchive"}
            archived["roles"] = {"producer": "A3", "reviewer": "A3", "approver": "A3"}
            raw.write_text(json.dumps(archived, sort_keys=True), encoding="utf-8")
            manifest = create_archive_manifest(
                "candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="https://evidence.example/archive-001"
            )
            candidate["archive"]["sha256"] = manifest["manifestSha256"]
            record["rawArchive"]["sha256"] = manifest["entries"][0]["sha256"]

            with patch.dict("os.environ", {"SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256": manifest["anchor"]["sha256"]}):
                result = audit_candidate(candidate, manifest, root)

            self.assertFalse(result.passed)
            self.assertTrue(any("roles" in error.lower() for error in result.errors))

    def test_raw_archive_hash_or_role_mismatch_blocks_audit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            changed = copy.deepcopy(candidate)
            changed["evidence"][0]["rawArchive"]["sha256"] = "f" * 64
            changed["evidence"][0]["roles"]["reviewer"] = "A3"
            with patch.dict("os.environ", {"SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256": manifest["anchor"]["sha256"]}):
                result = audit_candidate(changed, manifest, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("raw archive" in error.lower() for error in result.errors))
            self.assertTrue(any("independent" in error.lower() for error in result.errors))

    def test_candidate_audit_requires_an_out_of_band_anchor_digest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, _ = _candidate(root)
            record = candidate["evidence"][0]
            raw = root / "archive" / "record.json"
            raw.write_text(json.dumps({key: value for key, value in record.items() if key != "rawArchive"}, sort_keys=True), encoding="utf-8")
            manifest = create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="https://evidence.example/archive-001")
            candidate["archive"]["sha256"] = manifest["manifestSha256"]
            record["rawArchive"]["sha256"] = manifest["entries"][0]["sha256"]
            result = audit_candidate(candidate, manifest, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("trusted archive anchor" in error for error in result.errors))

    def test_anchor_rejects_replaced_archive_bytes_with_the_same_locator(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, _ = _candidate(root)
            record = candidate["evidence"][0]
            raw = root / "archive" / "record.json"
            raw.write_text(json.dumps({key: value for key, value in record.items() if key != "rawArchive"}, sort_keys=True), encoding="utf-8")
            manifest = create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="https://evidence.example/archive-001")
            candidate["archive"]["sha256"] = manifest["manifestSha256"]
            record["rawArchive"]["sha256"] = manifest["entries"][0]["sha256"]
            trusted_anchor = manifest["anchor"]["sha256"]
            raw.write_text(json.dumps({"replaced": True}), encoding="utf-8")
            manifest["entries"][0]["sha256"] = hashlib.sha256(raw.read_bytes()).hexdigest()
            manifest["entries"][0]["size"] = raw.stat().st_size
            unsigned = {key: value for key, value in manifest.items() if key != "manifestSha256"}
            manifest["manifestSha256"] = hashlib.sha256(json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
            candidate["archive"]["sha256"] = manifest["manifestSha256"]
            with patch.dict("os.environ", {"SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256": trusted_anchor}):
                result = audit_candidate(candidate, manifest, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("anchor" in error.lower() for error in result.errors))

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
