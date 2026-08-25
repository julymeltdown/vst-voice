from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.engineering_remediation_evidence import (  # noqa: E402
    REQUIRED_GATES,
    REQUIRED_MANUAL_QA,
    REQUIRED_REVIEW_LANES,
    validate_evidence,
)
from tools.engineering_remediation_contract import JsonObject  # noqa: E402


class EngineeringRemediationEvidenceTests(unittest.TestCase):
    def valid_record(self, root: Path) -> JsonObject:
        log = root / "logs/pass.txt"
        log.parent.mkdir(parents=True)
        log.write_text("verified candidate gate\n", encoding="utf-8")
        digest = hashlib.sha256(log.read_bytes()).hexdigest()
        archive = root / "archive/engineering-remediation.tar.zst"
        archive.parent.mkdir(parents=True)
        archive.write_bytes(b"sealed engineering evidence")
        archive_digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        candidate = "a" * 40
        base = "b" * 40
        gates = [
            {
                "id": gate,
                "candidateSha": candidate,
                "status": "PASS",
                "command": f"verify {gate}",
                "startedAt": "2026-08-25T00:00:00Z",
                "endedAt": "2026-08-25T00:01:00Z",
                "exitStatus": 0,
                "rawLog": {"locator": "logs/pass.txt", "sha256": digest},
            }
            for gate in REQUIRED_GATES
        ]
        manual_qa = [
            {"id": journey, "status": "PASS", "evidenceGate": "release-suite"}
            for journey in REQUIRED_MANUAL_QA
        ]
        review_lanes = [
            {
                "id": lane,
                "candidateSha": candidate,
                "verdict": "PASS",
                "report": {"locator": "logs/pass.txt", "sha256": digest},
            }
            for lane in REQUIRED_REVIEW_LANES
        ]
        return {
            "schemaVersion": 1,
            "recordKind": "ENGINEERING_REMEDIATION_EVIDENCE",
            "recordStatus": "SEALED",
            "reviewBaseSha": base,
            "candidateSha": candidate,
            "candidateTreeState": "CLEAN",
            "attestationCommitSha": None,
            "generatedAt": "2026-08-25T01:00:00Z",
            "environment": {
                "platform": "macOS",
                "architecture": "arm64",
                "compiler": "AppleClang 17.0.0",
                "cmake": "4.1.1",
                "python": "3.14.3",
                "generator": "Unix Makefiles",
                "redactedEnvironmentId": "c" * 64,
            },
            "gates": gates,
            "manualQa": manual_qa,
            "review": {
                "baseSha": base,
                "candidateSha": candidate,
                "lanes": review_lanes,
                "p0Count": 0,
                "p1Count": 0,
            },
            "evidenceArchive": {
                "sealed": True,
                "locator": "archive/engineering-remediation.tar.zst",
                "sha256": archive_digest,
            },
            "externalReleaseState": "BLOCKED",
        }

    def test_complete_candidate_bound_record_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            result = validate_evidence(record, root, containing_commit_sha="e" * 40)
        self.assertTrue(result.passed, result.errors)

    def test_short_missing_base_dirty_tree_and_zero_sha_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            record["reviewBaseSha"] = "short"
            record["candidateSha"] = "0" * 40
            record["candidateTreeState"] = "DIRTY"
            result = validate_evidence(record, root)
        self.assertFalse(result.passed)
        self.assertTrue(any("reviewBaseSha" in error for error in result.errors))
        self.assertTrue(any("candidateSha" in error for error in result.errors))
        self.assertTrue(any("CLEAN" in error for error in result.errors))

    def test_missing_and_digest_mismatched_logs_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            gates = record["gates"]
            self.assertIsInstance(gates, list)
            gates[0]["rawLog"]["locator"] = "logs/missing.txt"
            gates[1]["rawLog"]["sha256"] = "f" * 64
            result = validate_evidence(record, root)
        self.assertFalse(result.passed)
        self.assertTrue(any("missing" in error for error in result.errors))
        self.assertTrue(any("digest" in error for error in result.errors))

    def test_mixed_candidate_sha_and_incomplete_gate_set_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            gates = record["gates"]
            self.assertIsInstance(gates, list)
            gates[0]["candidateSha"] = "f" * 40
            gates.pop()
            result = validate_evidence(record, root)
        self.assertFalse(result.passed)
        self.assertTrue(any("candidate" in error for error in result.errors))
        self.assertTrue(any("gate set" in error for error in result.errors))

    def test_self_referential_containing_commit_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            candidate = record["candidateSha"]
            self.assertIsInstance(candidate, str)
            result = validate_evidence(
                record, root, containing_commit_sha=candidate
            )
        self.assertFalse(result.passed)
        self.assertTrue(any("contain" in error for error in result.errors))

    def test_non_pass_gate_and_review_findings_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            gates = record["gates"]
            review = record["review"]
            self.assertIsInstance(gates, list)
            self.assertIsInstance(review, dict)
            gates[0]["status"] = "NOT_RUN"
            review["p1Count"] = 1
            result = validate_evidence(record, root)
        self.assertFalse(result.passed)
        self.assertTrue(any("must be PASS" in error for error in result.errors))
        self.assertTrue(any("P0/P1" in error for error in result.errors))

    def test_non_string_ids_are_rejected_without_raising(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            gates = record["gates"]
            self.assertIsInstance(gates, list)
            gates[0]["id"] = {"malformed": "id"}
            result = validate_evidence(record, root)
        self.assertFalse(result.passed)
        self.assertTrue(any("IDs must be strings" in error for error in result.errors))

    def test_boolean_counts_and_exit_status_are_not_integers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            gates = record["gates"]
            review = record["review"]
            self.assertIsInstance(gates, list)
            self.assertIsInstance(review, dict)
            gates[0]["exitStatus"] = False
            review["p0Count"] = False
            result = validate_evidence(record, root)
        self.assertFalse(result.passed)
        self.assertTrue(any("exitStatus" in error for error in result.errors))
        self.assertTrue(any("P0/P1" in error for error in result.errors))

    def test_intermediate_symlink_in_log_path_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.valid_record(root)
            linked = root / "linked-logs"
            linked.symlink_to(root / "logs", target_is_directory=True)
            gates = record["gates"]
            self.assertIsInstance(gates, list)
            gates[0]["rawLog"]["locator"] = "linked-logs/pass.txt"
            result = validate_evidence(record, root)
        self.assertFalse(result.passed)
        self.assertTrue(any("symlink" in error for error in result.errors))

    def test_template_and_schema_define_candidate_attestation_boundary(self) -> None:
        schema = json.loads(
            (ROOT / "docs/product/engineering-remediation-evidence.schema.json").read_text(
                encoding="utf-8"
            )
        )
        template = json.loads(
            (ROOT / "docs/product/engineering-remediation-evidence-template.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(False, schema["additionalProperties"])
        self.assertIn("candidateSha", schema["required"])
        self.assertIn("attestationCommitSha", schema["required"])
        self.assertEqual("TEMPLATE", template["recordStatus"])
        self.assertEqual("BLOCKED", template["externalReleaseState"])
        self.assertEqual(list(REQUIRED_GATES), [gate["id"] for gate in template["gates"]])


if __name__ == "__main__":
    unittest.main()
