from __future__ import annotations

import copy
import unittest

from tests.external_beta.release_gate_fixtures import candidate as make_candidate
from tools.external_beta import release_gate


def _contract() -> release_gate.JsonObject:
    return {
        "schemaVersion": 1,
        "contractId": "project-seam.external-beta.acceptance",
        "requirements": [
            {
                "id": requirement_id,
                "evidencePolicy": {
                    "stageKinds": ["INSTALLED_TREE"],
                    "transformations": ["INSTALL"],
                    "minimumRecords": 1,
                    "requiredTargets": [
                        {
                            "platform": "macos",
                            "architecture": "arm64",
                            "surface": "standalone",
                            "host": None,
                        }
                    ],
                },
            }
            for requirement_id in release_gate.READY_REQUIREMENT_IDS
        ],
    }


def _bound_candidate(contract: release_gate.JsonObject) -> release_gate.JsonObject:
    candidate = make_candidate()
    digest = release_gate.sha256_json(contract)
    candidate["acceptanceContractSha256"] = digest
    candidate_root = candidate["candidateRoot"]
    assert isinstance(candidate_root, dict)
    candidate_root["acceptanceContractSha256"] = digest
    evidence = candidate["evidence"]
    assert isinstance(evidence, list)
    for record in evidence:
        assert isinstance(record, dict)
        record.update(
            {
                "stageNodeId": "installed-macos-001",
                "parentEdgeId": "edge-signed-to-installed-macos-001",
                "platform": "macos",
                "architecture": "arm64",
                "surface": "standalone",
                "host": None,
            }
        )
    return candidate


class ExternalBetaReleasePolicyTests(unittest.TestCase):
    def test_matching_requirement_policy_passes(self) -> None:
        contract = _contract()
        result = release_gate.evaluate_ready(_bound_candidate(contract), contract)
        self.assertTrue(result.passed, result.errors)

    def test_policy_rejects_wrong_stage_transformation_platform_and_surface(self) -> None:
        cases = (
            ("stage", "stageKinds", ["SIGNED_DELIVERABLE"]),
            ("transformation", "transformations", ["SIGN"]),
            (
                "platform",
                "requiredTargets",
                [
                    {
                        "platform": "windows",
                        "architecture": "x86_64",
                        "surface": "standalone",
                        "host": None,
                    }
                ],
            ),
            (
                "surface",
                "requiredTargets",
                [
                    {
                        "platform": "macos",
                        "architecture": "arm64",
                        "surface": "host",
                        "host": "reaper/CLAP",
                    }
                ],
            ),
        )
        for label, key, value in cases:
            with self.subTest(label=label):
                contract = _contract()
                requirements = contract["requirements"]
                assert isinstance(requirements, list)
                first = requirements[0]
                assert isinstance(first, dict)
                policy = first["evidencePolicy"]
                assert isinstance(policy, dict)
                policy[key] = value
                result = release_gate.evaluate_ready(
                    _bound_candidate(contract), contract
                )
                self.assertFalse(result.passed)
                self.assertTrue(
                    any(label in error for error in result.errors), result.errors
                )

    def test_policy_requires_every_declared_target(self) -> None:
        contract = _contract()
        requirements = contract["requirements"]
        assert isinstance(requirements, list)
        first = requirements[0]
        assert isinstance(first, dict)
        policy = first["evidencePolicy"]
        assert isinstance(policy, dict)
        targets = policy["requiredTargets"]
        assert isinstance(targets, list)
        targets.append(
            {
                "platform": "windows",
                "architecture": "x86_64",
                "surface": "standalone",
                "host": None,
            }
        )
        result = release_gate.evaluate_ready(_bound_candidate(contract), contract)
        self.assertFalse(result.passed)
        self.assertTrue(
            any("target coverage" in error for error in result.errors), result.errors
        )

    def test_candidate_cannot_substitute_a_weaker_contract(self) -> None:
        contract = _contract()
        candidate = _bound_candidate(contract)
        weaker = copy.deepcopy(contract)
        requirements = weaker["requirements"]
        assert isinstance(requirements, list)
        requirements.pop()
        result = release_gate.evaluate_ready(candidate, weaker)
        self.assertFalse(result.passed)
        self.assertTrue(
            any("acceptance contract" in error for error in result.errors),
            result.errors,
        )

    def test_candidate_root_must_bind_the_contract_digest(self) -> None:
        contract = _contract()
        candidate = _bound_candidate(contract)
        candidate_root = candidate["candidateRoot"]
        assert isinstance(candidate_root, dict)
        candidate_root["acceptanceContractSha256"] = "0" * 64
        result = release_gate.evaluate_ready(candidate, contract)
        self.assertFalse(result.passed)
        self.assertTrue(
            any("acceptance contract" in error for error in result.errors),
            result.errors,
        )


if __name__ == "__main__":
    unittest.main()
