from __future__ import annotations

import base64
import unittest

from tests.production.public_release_contract_fixtures import (
    OPERATION_KEY_ID,
    OPERATION_SEED,
    JsonObject,
    acceptance_contract,
    approval_seed,
    sha256_json,
    sign_operation,
    signing_payload,
)
from tests.production.public_release_fixtures import candidate
from tools.phase13a.update_contract import ed25519_sign
from tools.public_release import release_gate


def _rehash_operation(value: JsonObject) -> None:
    approvals = value["approvals"]
    operation = value["operationEnvelope"]
    assert isinstance(approvals, list)
    assert isinstance(operation, dict)
    operation["approvalEnvelopeSha256s"] = [
        item["envelopeSha256"] for item in approvals if isinstance(item, dict)
    ]
    sign_operation(operation, "envelopeSha256")


class PublicReleaseCryptoTests(unittest.TestCase):
    def test_valid_approval_key_cannot_claim_another_signer_identity(self) -> None:
        contract = acceptance_contract()
        value = candidate(contract)
        approvals = value["approvals"]
        assert isinstance(approvals, list)
        approval = approvals[0]
        assert isinstance(approval, dict)
        role = approval["role"]
        assert isinstance(role, str)
        approval["signerId"] = "forged-reviewer"
        approval["signature"] = base64.b64encode(
            ed25519_sign(
                signing_payload(approval, "envelopeSha256"),
                approval_seed(role),
            )
        ).decode("ascii")
        approval["envelopeSha256"] = sha256_json(
            {key: item for key, item in approval.items() if key != "envelopeSha256"}
        )
        _rehash_operation(value)

        result = release_gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertTrue(any("signer identity" in error for error in result.errors))

    def test_valid_operation_key_cannot_claim_another_actor_identity(self) -> None:
        contract = acceptance_contract()
        snapshot: JsonObject = {
            "schemaVersion": 1,
            "candidateLineageId": "public-lineage-001",
            "evidenceRootSha256": "a" * 64,
            "state": "PUBLIC_ACTIVE",
            "decisionLog": [],
        }
        decision: JsonObject = {
            "schemaVersion": 1,
            "decisionId": "pause-wrong-actor",
            "action": "PAUSE",
            "candidateLineageId": "public-lineage-001",
            "evidenceRootSha256": "a" * 64,
            "actorId": "forged-release-manager",
            "actorRole": "release-manager",
            "createdAt": "2026-08-31T02:00:00Z",
            "reason": "forged actor identity",
        }
        sign_operation(decision, "decisionSha256")

        with self.assertRaises(release_gate.ReleaseGateInputError):
            release_gate.transition(snapshot, decision, contract)

    def test_bit_flipped_approval_signature_blocks_activation(self) -> None:
        contract = acceptance_contract()
        value = candidate(contract)
        approvals = value["approvals"]
        assert isinstance(approvals, list)
        approval = approvals[0]
        assert isinstance(approval, dict)
        signature = approval["signature"]
        assert isinstance(signature, str)
        decoded = bytearray(base64.b64decode(signature))
        decoded[0] ^= 1
        approval["signature"] = base64.b64encode(decoded).decode("ascii")
        approval["signatureVerified"] = True
        approval["envelopeSha256"] = sha256_json(
            {key: item for key, item in approval.items() if key != "envelopeSha256"}
        )
        _rehash_operation(value)

        result = release_gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertIn("PR-013-approvals", result.blocked_ids)

    def test_arbitrary_operation_signature_blocks_even_with_true_boolean(self) -> None:
        contract = acceptance_contract()
        value = candidate(contract)
        operation = value["operationEnvelope"]
        assert isinstance(operation, dict)
        operation["signature"] = base64.b64encode(bytes(64)).decode("ascii")
        operation["signatureVerified"] = True
        operation["envelopeSha256"] = sha256_json(
            {key: item for key, item in operation.items() if key != "envelopeSha256"}
        )

        result = release_gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertIn("PR-013-approvals", result.blocked_ids)

    def test_arbitrary_operation_decision_signature_cannot_pause(self) -> None:
        snapshot: JsonObject = {
            "schemaVersion": 1,
            "candidateLineageId": "public-lineage-001",
            "evidenceRootSha256": "a" * 64,
            "state": "PUBLIC_ACTIVE",
            "decisionLog": [],
        }
        contract = acceptance_contract()
        decision: JsonObject = {
            "schemaVersion": 1,
            "decisionId": "pause-forged",
            "action": "PAUSE",
            "candidateLineageId": "public-lineage-001",
            "evidenceRootSha256": "a" * 64,
            "actorId": "release-operator-001",
            "actorRole": "release-manager",
            "createdAt": "2026-08-31T02:00:00Z",
            "reason": "forged operation",
        }
        sign_operation(decision, "decisionSha256")
        decision["signature"] = base64.b64encode(bytes(64)).decode("ascii")
        decision["signatureVerified"] = True
        decision["decisionSha256"] = sha256_json(
            {key: item for key, item in decision.items() if key != "decisionSha256"}
        )

        with self.assertRaises(release_gate.ReleaseGateInputError):
            release_gate.transition(snapshot, decision, contract)

    def test_release_manager_is_not_an_independent_quorum_role(self) -> None:
        self.assertNotIn("release-manager", release_gate.REQUIRED_APPROVAL_ROLES)
        self.assertIn(
            "independent-release-verifier",
            release_gate.REQUIRED_APPROVAL_ROLES,
        )

    def test_release_manager_operation_key_cannot_fill_quorum_slot(self) -> None:
        contract = acceptance_contract()
        value = candidate(contract)
        approvals = value["approvals"]
        assert isinstance(approvals, list)
        approval = approvals[0]
        assert isinstance(approval, dict)
        approval["keyId"] = OPERATION_KEY_ID
        approval["signature"] = base64.b64encode(
            ed25519_sign(
                signing_payload(approval, "envelopeSha256"),
                OPERATION_SEED,
            )
        ).decode("ascii")
        approval["envelopeSha256"] = sha256_json(
            {key: item for key, item in approval.items() if key != "envelopeSha256"}
        )
        _rehash_operation(value)

        result = release_gate.evaluate_gate(
            value,
            "PUBLIC_ACTIVE",
            contract,
            archive_verified=True,
        )

        self.assertFalse(result.passed)
        self.assertTrue(any("role-bound" in error for error in result.errors), result.errors)


if __name__ == "__main__":
    unittest.main()
