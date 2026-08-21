from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.external_beta import release_gate


def _record(
    *,
    requirement_id: str,
    candidate_root_id: str = "candidate-root-001",
    stage_node_id: str = "installed-macos-001",
    parent_edge_id: str = "edge-signed-to-installed-001",
    status: str = "PASS",
) -> dict[str, object]:
    return {
        "recordId": f"record-{requirement_id}",
        "requirementId": requirement_id,
        "candidateRootId": candidate_root_id,
        "stageNodeId": stage_node_id,
        "parentEdgeId": parent_edge_id,
        "sourceCommit": "a" * 40,
        "platform": "macos",
        "architecture": "arm64",
        "surface": "standalone",
        "host": None,
        "finalDeliverableSha256": "b" * 64,
        "installedTreeSha256": "c" * 64,
        "artifactSha256": "d" * 64,
        "toolIdentity": {"name": "validator", "version": "1.0"},
        "workloadId": "eb.render.preview.v1",
        "workloadSha256": release_gate.stable_workload_sha256("eb.render.preview.v1"),
        "machineProfileId": "eb.macos.arm64.reference.v1",
        "machineProfileSha256": release_gate.stable_machine_sha256("eb.macos.arm64.reference.v1"),
        "privacyClass": "PUBLIC_TECHNICAL",
        "roles": {"producer": "A3", "reviewer": "A6", "approver": "A3"},
        "trustedTime": "2026-08-21T12:00:00Z",
        "rawArchive": {
            "locator": f"archive/{requirement_id}.json",
            "sha256": "1" * 64,
        },
        "status": status,
        "blockingReason": None if status == "PASS" else "target not run",
    }


def _candidate() -> dict[str, object]:
    requirement_ids = tuple(release_gate.READY_REQUIREMENT_IDS)
    records = [_record(requirement_id=item) for item in requirement_ids]
    return {
        "schemaVersion": 1,
        "gate": "EXTERNAL_BETA_READY",
        "releaseIdentity": {
            "product": "Project SEAM",
            "version": "0.13.1",
            "buildId": "candidate-build-001",
            "sourceCommit": "a" * 40,
            "buildEpoch": 1_755_768_000,
        },
        "candidateRoot": {
            "id": "candidate-root-001",
            "sha256": "2" * 64,
            "status": "SEALED",
        },
        "stageNodes": [
            {"id": "unsigned-001", "kind": "UNSIGNED_PAYLOAD", "sha256": "3" * 64},
            {"id": "signed-001", "kind": "SIGNED_DELIVERABLE", "sha256": "4" * 64},
            {"id": "installed-macos-001", "kind": "INSTALLED_TREE", "sha256": "c" * 64},
        ],
        "stageEdges": [
            {"id": "edge-unsigned-to-signed-001", "parent": "unsigned-001", "child": "signed-001", "transformation": "SIGN"},
            {"id": "edge-signed-to-installed-001", "parent": "signed-001", "child": "installed-macos-001", "transformation": "INSTALL"},
        ],
        "requirements": {
            item: {"status": "PASS", "evidenceRecordIds": [f"record-{item}"]}
            for item in requirement_ids
        },
        "evidence": records,
        "workloadCatalog": {
            "eb.render.preview.v1": {
                "sha256": release_gate.stable_workload_sha256("eb.render.preview.v1"),
                "identityMode": "stable-id-v1",
            },
        },
        "machineProfiles": {
            "eb.macos.arm64.reference.v1": {
                "sha256": release_gate.stable_machine_sha256("eb.macos.arm64.reference.v1"),
                "identityMode": "stable-id-v1",
            },
        },
        "archive": {
            "locator": "archive/candidate-root-001",
            "sha256": "5" * 64,
            "anchored": True,
            "immutable": True,
        },
        "issues": [],
    }


class ExternalBetaReleaseGateTests(unittest.TestCase):
    def test_complete_g3_like_matrix_does_not_promote_without_external_beta_evidence(self) -> None:
        candidate = _candidate()
        candidate["requirements"] = {}
        result = release_gate.evaluate_ready(candidate)
        self.assertFalse(result.passed)
        self.assertEqual("EXTERNAL_BETA_READY", result.state)
        self.assertTrue(any("requirement" in error for error in result.errors))

    def test_stage_lineage_mismatch_blocks_even_when_record_says_pass(self) -> None:
        candidate = _candidate()
        evidence = candidate["evidence"]
        assert isinstance(evidence, list)
        evidence[0]["stageNodeId"] = "unlisted-stage"
        result = release_gate.evaluate_ready(candidate)
        self.assertFalse(result.passed)
        self.assertTrue(any("stage" in error.lower() for error in result.errors))

    def test_workload_and_machine_identity_are_required_for_pass(self) -> None:
        candidate = _candidate()
        evidence = candidate["evidence"]
        assert isinstance(evidence, list)
        evidence[0]["workloadSha256"] = "0" * 64
        result = release_gate.evaluate_ready(candidate)
        self.assertFalse(result.passed)
        self.assertTrue(any("workload" in error.lower() for error in result.errors))

    def test_ready_candidate_cannot_close_without_external_cohort_coverage(self) -> None:
        result = release_gate.evaluate_closed(_candidate())
        self.assertFalse(result.passed)
        self.assertEqual("EXTERNAL_BETA_CLOSED", result.state)
        self.assertTrue(any("cohort" in error.lower() for error in result.errors))

    def test_close_requires_terminal_assignments_and_all_claimed_hosts(self) -> None:
        candidate = _candidate()
        candidate["cohort"] = {
            "evaluationWindow": {"status": "ENDED", "endedAt": "2026-08-21T18:00:00Z"},
            "externalSessions": [
                {"participantId": "p1", "platform": "macos", "status": "COMPLETED", "flows": ["F1", "F2", "F5"]},
                {"participantId": "p2", "platform": "windows", "status": "COMPLETED", "flows": ["F1", "F2", "F5"]},
            ],
            "claimedHostTuples": ["macos/reaper/7.0/CLAP", "windows/reaper/7.0/CLAP"],
            "hostSessions": [
                {"tuple": "macos/reaper/7.0/CLAP", "participantId": "p1", "status": "COMPLETED"},
                {"tuple": "windows/reaper/7.0/CLAP", "participantId": "p2", "status": "COMPLETED"},
            ],
            "assignments": [
                {"participantId": "p1", "status": "COMPLETED", "reason": "finished"},
                {"participantId": "p2", "status": "COMPLETED", "reason": "finished"},
            ],
            "checkpoints": [{"id": "cp1", "status": "RESOLVED"}],
            "incidents": [],
            "approvals": [
                {"role": "A3", "status": "APPROVED"},
                {"role": "A4", "status": "APPROVED"},
            ],
        }
        result = release_gate.evaluate_closed(candidate)
        self.assertTrue(result.passed, result.errors)

    def test_canonical_json_is_stable_and_hashable(self) -> None:
        value = {"z": 1, "a": [True, "한글"]}
        encoded = release_gate.canonical_json(value)
        self.assertEqual('{"a":[true,"한글"],"z":1}', encoded)
        self.assertEqual(
            hashlib.sha256(encoded.encode("utf-8")).hexdigest(),
            release_gate.sha256_json(value),
        )


if __name__ == "__main__":
    unittest.main()
