from __future__ import annotations

import hashlib
import unittest

from tools.external_beta import release_gate  # noqa: E402
from tests.external_beta.cohort_fixtures import closed_cohort as _closed_cohort
from tests.external_beta.release_gate_fixtures import candidate as _candidate


class ExternalBetaReleaseGateTests(unittest.TestCase):
    def test_ready_passes_when_every_requirement_references_matching_pass_evidence(self) -> None:
        result = release_gate.evaluate_ready(_candidate())
        self.assertTrue(result.passed, result.errors)

    def test_ready_rejects_referenced_evidence_without_matching_pass_status(self) -> None:
        cases = (
            ("not-run", "status", "NOT_RUN"),
            ("blocked", "status", "BLOCKED"),
            ("failed", "status", "FAIL"),
            ("other-requirement", "requirementId", "EB-002-identity"),
        )
        for label, field, value in cases:
            with self.subTest(label=label):
                candidate = _candidate()
                evidence = candidate["evidence"]
                assert isinstance(evidence, list)
                record = evidence[0]
                assert isinstance(record, dict)
                record[field] = value
                if field == "status":
                    record["blockingReason"] = "evidence did not pass"
                result = release_gate.evaluate_ready(candidate)
                self.assertFalse(result.passed)
                self.assertTrue(any("PASS evidence for itself" in error for error in result.errors))

    def test_ready_rejects_referenced_pass_evidence_from_another_candidate_or_release(self) -> None:
        cases = (
            ("candidate", "candidateRootId", "candidate-root-002"),
            ("release", "sourceCommit", "b" * 40),
        )
        for label, field, value in cases:
            with self.subTest(label=label):
                candidate = _candidate()
                evidence = candidate["evidence"]
                assert isinstance(evidence, list)
                record = evidence[0]
                assert isinstance(record, dict)
                record[field] = value
                result = release_gate.evaluate_ready(candidate)
                self.assertFalse(result.passed)

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

    def test_ready_rejects_evidence_hashes_not_bound_to_the_stage_graph(self) -> None:
        candidate = _candidate()
        evidence = candidate["evidence"]
        assert isinstance(evidence, list)
        installed = next(
            record
            for record in evidence
            if isinstance(record, dict)
            and record.get("stageNodeId") == "installed-macos-001"
        )
        installed["installedTreeSha256"] = "f" * 64

        result = release_gate.evaluate_ready(candidate)

        self.assertFalse(result.passed)
        self.assertTrue(any("installed tree" in error.lower() for error in result.errors))

    def test_ready_rejects_final_artifact_hashes_not_bound_to_signed_stage(self) -> None:
        candidate = _candidate()
        evidence = candidate["evidence"]
        assert isinstance(evidence, list)
        record = next(
            item
            for item in evidence
            if isinstance(item, dict)
            and item.get("stageNodeId") == "installed-macos-001"
        )
        record["finalDeliverableSha256"] = "f" * 64
        record["artifactSha256"] = "f" * 64

        result = release_gate.evaluate_ready(candidate)

        self.assertFalse(result.passed)
        self.assertTrue(any("signed deliverable" in error.lower() for error in result.errors))

    def test_ready_candidate_cannot_close_without_external_cohort_coverage(self) -> None:
        result = release_gate.evaluate_closed(_candidate())
        self.assertFalse(result.passed)
        self.assertEqual("EXTERNAL_BETA_CLOSED", result.state)
        self.assertTrue(any("cohort" in error.lower() for error in result.errors))

    def test_close_requires_terminal_assignments_and_all_claimed_hosts(self) -> None:
        candidate = _candidate()
        candidate["cohort"] = _closed_cohort()
        result = release_gate.evaluate_closed(candidate)
        self.assertTrue(result.passed, result.errors)

    def test_closed_gate_delegates_to_strict_cohort_contract(self) -> None:
        candidate = _candidate()
        cohort = _closed_cohort()
        cohort["assignments"][0]["email"] = "person@example.com"
        candidate["cohort"] = cohort
        result = release_gate.evaluate_closed(candidate)
        self.assertFalse(result.passed)
        self.assertTrue(any("PII" in error for error in result.errors))

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
