from __future__ import annotations

import json
import copy
import hashlib
import subprocess
import sys
import unittest
import tempfile
from pathlib import Path

from tools.external_beta.operations import can_distribute, transition

ROOT = Path(__file__).resolve().parents[2]


def _snapshot() -> dict:
    return {"schemaVersion": 1, "candidateRootId": "candidate-root-001", "state": "FROZEN", "decisionLog": []}


def _decision(action: str, decision_id: str, **extra: object) -> dict:
    return {"schemaVersion": 1, "decisionId": decision_id, "action": action, "candidateRootId": "candidate-root-001", "actorRole": "A3", "createdAt": "2026-08-22T01:00:00Z", **extra}


class OperationsTests(unittest.TestCase):
    def test_claimed_audit_booleans_cannot_authorize_any_distributable_transition(self) -> None:
        for state, action in (("FROZEN", "PROMOTE_READY"), ("READY", "START_COHORT"),
                              ("DISTRIBUTION_PAUSED", "RESUME"), ("COHORT_ACTIVE", "CLOSE")):
            with self.subTest(action=action):
                snapshot = _snapshot() | {"state": state}
                before = copy.deepcopy(snapshot)
                with self.assertRaisesRegex(ValueError, "candidateRootSha256"):
                    transition(snapshot, _decision(action, "d1", auditPassed=True, freshGo=True,
                        cohortAuditPassed=True, evaluationWindowEnded=True,
                        consentVersion="consent-1", approvals=["A3", "A4"]))
                self.assertEqual(before, snapshot)

    def test_pause_remains_available_without_release_evidence(self) -> None:
        snapshot = _snapshot() | {"state": "COHORT_ACTIVE"}
        paused = transition(snapshot, _decision("PAUSE", "pause", reason="integrity review"))
        self.assertEqual("DISTRIBUTION_PAUSED", paused["state"])
        self.assertFalse(can_distribute(paused["state"]))

    def test_revoke_is_irreversible_and_blocks_distribution(self) -> None:
        snapshot = _snapshot() | {"state": "READY"}
        revoked = transition(snapshot, _decision("REVOKE", "d2", reason="key compromise"))
        self.assertEqual("REVOKED", revoked["state"])
        self.assertFalse(can_distribute(revoked["state"]))
        with self.assertRaises(ValueError):
            transition(revoked, _decision("RESUME", "d3", freshGo=True, approvals=["A3", "A4"]))

    def test_promotion_and_resume_require_authority_and_unique_decisions(self) -> None:
        with self.assertRaises(ValueError):
            transition(_snapshot(), _decision("PROMOTE_READY", "d1", auditPassed=False, approvals=["A3"]))
        snapshot = _snapshot() | {"decisionLog": [_decision("PAUSE", "d1", reason="prior decision")]}
        with self.assertRaises(ValueError):
            transition(snapshot, _decision("PROMOTE_READY", "d1", auditPassed=True, approvals=["A3", "A4"]))

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
            decision = _decision("PROMOTE_READY", "d1", approvals=["A3", "A4"],
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
            transition(snapshot, _decision("PROMOTE_READY", "d1", approvals=["A3", "A4"],
                candidateRootSha256="a" * 64, auditPassed=True))
        with self.assertRaisesRegex(ValueError, "candidateRootSha256"):
            transition(snapshot, _decision("PROMOTE_READY", "d1", approvals=["A3", "A4"],
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
