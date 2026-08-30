from __future__ import annotations

import unittest

from tests.production.public_release_contract_fixtures import (
    APPROVAL_ROLES,
    JsonObject,
    JsonValue,
    acceptance_contract,
    approval,
    sign_operation,
)
from tests.production.public_release_fixtures import candidate
from tools.public_release import release_gate


def _decision(
    action: str,
    decision_id: str,
    created_at: str,
    **extra: JsonValue,
) -> JsonObject:
    value: JsonObject = {
        "schemaVersion": 1,
        "decisionId": decision_id,
        "action": action,
        "candidateLineageId": "public-lineage-001",
        "evidenceRootSha256": "a" * 64,
        "actorId": "release-operator-001",
        "actorRole": "release-manager",
        "createdAt": created_at,
        "reason": "contract drill",
        **extra,
    }
    sign_operation(value, "decisionSha256")
    return value


class PublicReleaseStateMachineTests(unittest.TestCase):
    def test_public_state_vocabulary_is_exact(self) -> None:
        self.assertEqual(
            (
                "DRAFT",
                "AUTHORIZED_FROZEN",
                "SIGNED",
                "CLEAN_INSTALLED",
                "BANK_READY",
                "EVIDENCE_PASSED",
                "EXTERNAL_BETA_READY",
                "EXTERNAL_BETA_CLOSED",
                "PUBLIC_ACTIVE",
                "DISTRIBUTION_PAUSED",
                "SUPERSEDED",
                "REVOKED",
            ),
            release_gate.PUBLIC_STATES,
        )

    def test_pause_resume_needs_a_fresh_quorum_and_revoke_is_terminal(self) -> None:
        contract = acceptance_contract()
        active: JsonObject = {
            "schemaVersion": 1,
            "candidateLineageId": "public-lineage-001",
            "evidenceRootSha256": "a" * 64,
            "state": "PUBLIC_ACTIVE",
            "decisionLog": [],
        }
        pause = _decision("PAUSE", "pause-001", "2026-08-31T02:00:00Z")

        paused = release_gate.transition(active, pause, contract)

        self.assertEqual("DISTRIBUTION_PAUSED", paused["state"])
        stale_resume = _decision(
            "RESUME",
            "resume-stale",
            "2026-08-31T03:00:00Z",
            approvals=candidate()["approvals"],
            gatePassed=True,
        )
        with self.assertRaises(release_gate.ReleaseGateInputError):
            release_gate.transition(paused, stale_resume, contract)
        fresh_approvals = [
            approval(role, index, "a" * 64, "2026-08-31T02:30:00Z")
            for index, role in enumerate(APPROVAL_ROLES, start=1)
        ]
        resume = _decision(
            "RESUME",
            "resume-fresh",
            "2026-08-31T03:00:00Z",
            approvals=fresh_approvals,
            gatePassed=True,
        )
        resumed = release_gate.transition(paused, resume, contract)
        revoked = release_gate.transition(
            resumed,
            _decision("REVOKE", "revoke-001", "2026-08-31T04:00:00Z"),
            contract,
        )

        self.assertEqual("REVOKED", revoked["state"])
        with self.assertRaises(release_gate.ReleaseGateInputError):
            release_gate.transition(revoked, resume, contract)


if __name__ == "__main__":
    unittest.main()
