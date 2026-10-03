from __future__ import annotations

import json
import copy
import hashlib
import base64
import subprocess
import sys
import unittest
import tempfile
from pathlib import Path

from tools.external_beta.operations import can_distribute, transition
from tools.phase13a.update_contract import (
    canonical_json as signing_json,
    ed25519_public_key,
    ed25519_sign,
)
from tools.public_release.contracts import sha256_json

ROOT = Path(__file__).resolve().parents[2]

POLICY_VERSION = "beta-operations-1"


def _seed(role: str) -> bytes:
    return hashlib.sha256(f"beta-operations-seed:{role}".encode()).digest()


def _key_id(role: str) -> str:
    return f"beta-operations-{role}"


def operation_policy() -> dict:
    return {
        "policyVersion": POLICY_VERSION,
        "requiredRoles": ["A3", "A4", "A5", "A6"],
        "trustedKeys": [
            {"keyId": _key_id(role), "role": role, "signerId": f"beta-{role}",
             "publicKey": base64.b64encode(bytes.fromhex(_public_hex(role))).decode("ascii")}
            for role in ("A3", "A4", "A5", "A6")
        ],
    }


def _public_hex(role: str) -> str:
    return ed25519_public_key(_seed(role)).hex()


def _sign(value: dict, digest_field: str, role: str) -> None:
    value.pop("signature", None)
    value.pop(digest_field, None)
    value["keyId"] = _key_id(role)
    value["policyVersion"] = POLICY_VERSION
    value["algorithm"] = "Ed25519"
    unsigned = {k: v for k, v in value.items() if k not in {"signature", digest_field}}
    value["signature"] = base64.b64encode(ed25519_sign(signing_json(unsigned), _seed(role))).decode("ascii")
    value[digest_field] = sha256_json(value)


def approval(role: str, decision_id: str) -> dict:
    value = {
        "role": role,
        "approverId": f"beta-{role}",
        "decision": "GO",
        "decisionId": decision_id,
    }
    _sign(value, "approvalSha256", role)
    return value


def signed_approvals(*roles: str, decision_id: str = "d1") -> list:
    return [approval(role, decision_id) for role in roles]


def _snapshot() -> dict:
    return {"schemaVersion": 1, "candidateRootId": "candidate-root-001", "state": "FROZEN", "decisionLog": []}


def _decision(action: str, decision_id: str, **extra: object) -> dict:
    return {"schemaVersion": 1, "decisionId": decision_id, "action": action, "candidateRootId": "candidate-root-001", "actorRole": "A3", "createdAt": "2026-08-22T01:00:00Z", **extra}


def _signed_decision(action: str, decision_id: str, *, role: str = "A3", **extra: object) -> dict:
    """A decision whose own actor signature verifies against the role-bound trusted key."""
    value = _decision(action, decision_id, **extra)
    value["actorRole"] = role
    value["actorId"] = f"beta-{role}"
    value["operationPolicy"] = operation_policy()
    _sign(value, "decisionSha256", role)
    return value


class OperationsTests(unittest.TestCase):
    def test_role_lists_cannot_authorize_a_transition(self) -> None:
        # The previous contract accepted any list containing "A3" plus "A4" or "A6". Anyone who
        # could write the snapshot could therefore promote, resume, pause and revoke a candidate.
        for action, state in (("PROMOTE_READY", "FROZEN"), ("RESUME", "DISTRIBUTION_PAUSED"),
                              ("PAUSE", "COHORT_ACTIVE"), ("REVOKE", "READY")):
            with self.subTest(action=action):
                snapshot = _snapshot() | {"state": state}
                before = copy.deepcopy(snapshot)
                forged = _decision(action, "forged", reason="looks official",
                                   consentVersion="consent-1", evaluationWindowEnded=True,
                                   approvals=["A3", "A4"])
                with self.assertRaises(ValueError):
                    transition(snapshot, forged)
                self.assertEqual(before, snapshot)

    def test_a_forged_signature_cannot_pause_or_revoke(self) -> None:
        for action, state in (("PAUSE", "COHORT_ACTIVE"), ("REVOKE", "READY")):
            with self.subTest(action=action):
                snapshot = _snapshot() | {"state": state}
                before = copy.deepcopy(snapshot)
                decision = _signed_decision(action, action + "-1", reason="forged")
                decision["signature"] = base64.b64encode(bytes(64)).decode("ascii")
                with self.assertRaisesRegex(ValueError, "signature"):
                    transition(snapshot, decision)
                self.assertEqual(before, snapshot)

    def test_a_quorum_missing_the_required_role_is_refused(self) -> None:
        # Both existing promotion cases supply A3, so removing the required-role check was
        # invisible to them. A quorum of signed-but-wrong roles must still be refused.
        snapshot = _snapshot()
        before = copy.deepcopy(snapshot)
        with self.assertRaisesRegex(ValueError, "A3 approval is required"):
            transition(snapshot, _decision("PROMOTE_READY", "d1", consentVersion="consent-1",
                                           operationPolicy=operation_policy(),
                                           approvals=signed_approvals("A4", "A6", decision_id="d1")))
        self.assertEqual(before, snapshot)

    def test_a_valid_key_cannot_claim_another_actor_identity(self) -> None:
        snapshot = _snapshot() | {"state": "COHORT_ACTIVE"}
        decision = _signed_decision("PAUSE", "pause-impostor", reason="integrity review")
        decision["actorId"] = "beta-A4"
        decision.pop("decisionSha256")
        decision.pop("signature")
        _sign(decision, "decisionSha256", "A3")
        with self.assertRaisesRegex(ValueError, "signer identity"):
            transition(snapshot, decision)

    def test_a_quorum_cannot_be_assembled_from_unsigned_role_strings(self) -> None:
        signed = approval("A3", "d1")
        unsigned = {"role": "A4", "approverId": "beta-A4", "decision": "GO", "decisionId": "d1"}
        snapshot = _snapshot()
        before = copy.deepcopy(snapshot)
        with self.assertRaisesRegex(ValueError, "not authoritative"):
            transition(snapshot, _decision("PROMOTE_READY", "d1", consentVersion="consent-1",
                                           operationPolicy=operation_policy(),
                                           approvals=[signed, unsigned]))
        self.assertEqual(before, snapshot)

    def test_claimed_audit_booleans_cannot_authorize_any_distributable_transition(self) -> None:
        for state, action in (("FROZEN", "PROMOTE_READY"), ("READY", "START_COHORT"),
                              ("DISTRIBUTION_PAUSED", "RESUME"), ("COHORT_ACTIVE", "CLOSE")):
            with self.subTest(action=action):
                snapshot = _snapshot() | {"state": state}
                before = copy.deepcopy(snapshot)
                with self.assertRaisesRegex(ValueError, "candidateRootSha256"):
                    transition(snapshot, _decision(action, "d1", auditPassed=True, freshGo=True,
                        cohortAuditPassed=True, evaluationWindowEnded=True,
                        consentVersion="consent-1", operationPolicy=operation_policy(), approvals=signed_approvals("A3", "A4", decision_id="d1")))
                self.assertEqual(before, snapshot)

    def test_pause_remains_available_without_release_evidence(self) -> None:
        snapshot = _snapshot() | {"state": "COHORT_ACTIVE"}
        paused = transition(snapshot, _signed_decision("PAUSE", "pause", reason="integrity review"))
        self.assertEqual("DISTRIBUTION_PAUSED", paused["state"])
        self.assertFalse(can_distribute(paused["state"]))

    def test_revoke_is_irreversible_and_blocks_distribution(self) -> None:
        snapshot = _snapshot() | {"state": "READY"}
        revoked = transition(snapshot, _signed_decision("REVOKE", "d2", reason="key compromise"))
        self.assertEqual("REVOKED", revoked["state"])
        self.assertFalse(can_distribute(revoked["state"]))
        with self.assertRaises(ValueError):
            transition(revoked, _decision("RESUME", "d3", freshGo=True, operationPolicy=operation_policy(), approvals=signed_approvals("A3", "A4", decision_id="d1")))

    def test_promotion_and_resume_require_authority_and_unique_decisions(self) -> None:
        with self.assertRaises(ValueError):
            transition(_snapshot(), _decision("PROMOTE_READY", "d1", auditPassed=False, operationPolicy=operation_policy(), approvals=signed_approvals("A3", decision_id="d1")))
        snapshot = _snapshot() | {"decisionLog": [_decision("PAUSE", "d1", reason="prior decision")]}
        with self.assertRaises(ValueError):
            transition(snapshot, _decision("PROMOTE_READY", "d1", auditPassed=True, operationPolicy=operation_policy(), approvals=signed_approvals("A3", "A4", decision_id="d1")))

    def test_references_are_rehashed_and_the_underlying_release_audit_runs(self) -> None:
        from tests.external_beta.test_release_audit import _candidate
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate, manifest = _candidate(root)
            def save(name, value):
                path = root / name
                path.write_text(json.dumps(value), encoding="utf-8")
                return {"locator": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            snapshot = _snapshot() | {"candidateRootSha256": candidate["candidateRoot"]["sha256"]}
            decision = _decision("PROMOTE_READY", "d1", operationPolicy=operation_policy(), approvals=signed_approvals("A3", "A4", decision_id="d1"),
                candidateRootSha256=snapshot["candidateRootSha256"], releaseAudit={
                    "candidate": save("candidate.json", candidate), "archiveManifest": save("manifest.json", manifest),
                    "archiveRoot": "."})
            with self.assertRaisesRegex(ValueError, "reproduced release audit failed") as caught:
                transition(snapshot, decision, base=root)
            self.assertIn("trusted archive anchor", str(caught.exception))
            self.assertEqual("FROZEN", snapshot["state"])
            (root / "candidate.json").write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "digest"):
                transition(snapshot, decision, base=root)

    def test_missing_and_wrong_root_audit_inputs_fail_without_mutation(self) -> None:
        snapshot = _snapshot() | {"candidateRootSha256": "a" * 64}
        with self.assertRaisesRegex(ValueError, "restored archiveRoot"):
            transition(snapshot, _decision("PROMOTE_READY", "d1", operationPolicy=operation_policy(), approvals=signed_approvals("A3", "A4", decision_id="d1"),
                candidateRootSha256="a" * 64, auditPassed=True))
        with self.assertRaisesRegex(ValueError, "candidateRootSha256"):
            transition(snapshot, _decision("PROMOTE_READY", "d1", operationPolicy=operation_policy(), approvals=signed_approvals("A3", "A4", decision_id="d1"),
                candidateRootSha256="b" * 64))

    def test_cli_expect_blocked_is_observable(self) -> None:
        snapshot_path = ROOT / ".omo" / "tmp-operation-snapshot.json"
        decision_path = ROOT / ".omo" / "tmp-operation-decision.json"
        try:
            snapshot_path.parent.mkdir(parents=True, exist_ok=True)
            snapshot_path.write_text(json.dumps(_snapshot()), encoding="utf-8")
            decision_path.write_text(json.dumps(_decision("CLOSE", "d1")), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(ROOT / "scripts/run_external_beta_operation.py"), "--snapshot", str(snapshot_path), "--decision", str(decision_path), "--expect-blocked"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            self.assertIn('"passed": false', completed.stdout)
        finally:
            snapshot_path.unlink(missing_ok=True)
            decision_path.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
