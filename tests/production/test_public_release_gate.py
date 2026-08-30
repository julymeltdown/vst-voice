from __future__ import annotations

import importlib
import importlib.util
from types import ModuleType
import unittest

from tests.production.public_release_fixtures import (
    acceptance_contract,
    approval,
    candidate,
    sha256_json,
    sign_operation,
)


class PublicReleaseGateTests(unittest.TestCase):
    def _gate(self) -> ModuleType:
        try:
            spec = importlib.util.find_spec("tools.public_release.release_gate")
        except ModuleNotFoundError:
            spec = None
        self.assertIsNotNone(
            spec,
            "tools.public_release.release_gate must implement the public gate",
        )
        return importlib.import_module("tools.public_release.release_gate")

    def test_empty_candidate_blocks_every_public_category(self) -> None:
        gate = self._gate()

        result = gate.evaluate_gate({}, "PUBLIC_ACTIVE", archive_verified=False)

        self.assertFalse(result.passed)
        self.assertEqual("PUBLIC_ACTIVE", result.state)
        self.assertEqual(gate.PUBLIC_REQUIREMENT_IDS, result.blocked_ids)

    def test_complete_public_fixture_reaches_active_after_archive_audit(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()

        result = gate.evaluate_gate(
            candidate(contract),
            "PUBLIC_ACTIVE",
            acceptance_contract=contract,
            archive_verified=True,
        )

        self.assertTrue(result.passed, result.errors)
        self.assertEqual((), result.blocked_ids)

    def test_closed_beta_without_public_documents_or_channel_stays_blocked(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()
        value = candidate(contract)
        value.pop("publicDocuments")
        value.pop("updateChannel")

        result = gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            acceptance_contract=contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertIn("PR-004-public-documents", result.blocked_ids)
        self.assertIn("PR-009-update-channel", result.blocked_ids)

    def test_blocked_acceptance_contract_cannot_activate_complete_candidate(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()
        contract["status"] = "BLOCKED"
        value = candidate(contract)

        result = gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            acceptance_contract=contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertIn("PR-001-contract", result.blocked_ids)

    def test_draft_public_documents_block_activation(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()
        documents = contract["publicDocuments"]
        assert isinstance(documents, list)
        document = documents[0]
        assert isinstance(document, dict)
        document["approvalStatus"] = "DRAFT"
        value = candidate(contract)

        result = gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            acceptance_contract=contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertIn("PR-004-public-documents", result.blocked_ids)

    def test_root_chain_rejects_source_bank_installer_and_archive_mismatches(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()
        cases = (
            ("source", ("releaseIdentity", "sourceCommit"), "b" * 40),
            ("bank", ("bank", "sourceSha256"), "f" * 64),
            ("installer", ("signedArtifacts", "windowsInstallerSha256"), "f" * 64),
            ("archive", ("archive", "manifestSha256"), "f" * 64),
        )
        for label, path, replacement in cases:
            with self.subTest(label=label):
                value = candidate(contract)
                parent = value[path[0]]
                assert isinstance(parent, dict)
                parent[path[1]] = replacement

                result = gate.evaluate_gate(
                    value,
                    "PUBLIC_ACTIVE",
                    acceptance_contract=contract,
                    archive_verified=True,
                )

                self.assertFalse(result.passed)
                self.assertTrue(
                    any(label in error.lower() for error in result.errors),
                    result.errors,
                )

    def test_external_beta_document_digest_cannot_satisfy_public_document(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()
        value = candidate(contract)
        documents = value["publicDocuments"]
        assert isinstance(documents, list)
        document = documents[0]
        assert isinstance(document, dict)
        document["acceptedSha256"] = "e" * 64

        result = gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            acceptance_contract=contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertIn("PR-004-public-documents", result.blocked_ids)

    def test_one_signer_cannot_claim_two_approval_roles(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()
        value = candidate(contract)
        approvals = value["approvals"]
        operation = value["operationEnvelope"]
        assert isinstance(approvals, list)
        assert isinstance(operation, dict)
        first = approvals[0]
        second = approvals[1]
        assert isinstance(first, dict)
        assert isinstance(second, dict)
        second["signerId"] = first["signerId"]
        second["envelopeSha256"] = sha256_json(
            {key: item for key, item in second.items() if key != "envelopeSha256"}
        )
        operation["approvalEnvelopeSha256s"] = [
            item["envelopeSha256"] for item in approvals if isinstance(item, dict)
        ]
        operation["envelopeSha256"] = sha256_json(
            {key: item for key, item in operation.items() if key != "envelopeSha256"}
        )

        result = gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            acceptance_contract=contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertIn("PR-013-approvals", result.blocked_ids)
        self.assertTrue(any("distinct" in error.lower() for error in result.errors))

    def test_approval_reissue_does_not_change_the_evidence_root(self) -> None:
        gate = self._gate()
        contract = acceptance_contract()
        value = candidate(contract)
        root_chain = value["rootChain"]
        approvals = value["approvals"]
        operation = value["operationEnvelope"]
        assert isinstance(root_chain, dict)
        assert isinstance(approvals, list)
        assert isinstance(operation, dict)
        evidence_root = root_chain["evidenceRoot"]
        assert isinstance(evidence_root, dict)
        original_root = evidence_root["sha256"]
        approvals[0] = approval(
            "independent-release-verifier",
            1,
            str(evidence_root["sha256"]),
            "2026-08-31T01:05:00Z",
        )
        operation["approvalEnvelopeSha256s"] = [
            item["envelopeSha256"] for item in approvals if isinstance(item, dict)
        ]
        sign_operation(operation, "envelopeSha256")

        result = gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            acceptance_contract=contract,
            archive_verified=True,
        )

        self.assertTrue(result.passed, result.errors)
        self.assertEqual(original_root, evidence_root["sha256"])

if __name__ == "__main__":
    unittest.main()
