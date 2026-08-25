from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

from tools.external_beta.operations import can_distribute, transition

ROOT = Path(__file__).resolve().parents[2]


def _snapshot() -> dict:
    return {"schemaVersion": 1, "candidateRootId": "candidate-root-001", "state": "FROZEN", "decisionLog": []}


def _decision(action: str, decision_id: str, **extra: object) -> dict:
    return {"schemaVersion": 1, "decisionId": decision_id, "action": action, "candidateRootId": "candidate-root-001", "actorRole": "A3", "createdAt": "2026-08-22T01:00:00Z", **extra}


class OperationsTests(unittest.TestCase):
    def test_authorized_lifecycle_allows_pause_resume_and_close(self) -> None:
        snapshot = _snapshot()
        snapshot = transition(snapshot, _decision("PROMOTE_READY", "d1", auditPassed=True, approvals=["A3", "A4"]))
        snapshot = transition(snapshot, _decision("START_COHORT", "d2", consentVersion="consent-1"))
        self.assertTrue(can_distribute(snapshot["state"]))
        snapshot = transition(snapshot, _decision("PAUSE", "d3", reason="integrity review"))
        self.assertFalse(can_distribute(snapshot["state"]))
        snapshot = transition(snapshot, _decision("RESUME", "d4", freshGo=True, approvals=["A3", "A6"]))
        snapshot = transition(snapshot, _decision("CLOSE", "d5", cohortAuditPassed=True, evaluationWindowEnded=True))
        self.assertEqual("CLOSED", snapshot["state"])
        self.assertFalse(can_distribute(snapshot["state"]))

    def test_revoke_is_irreversible_and_blocks_distribution(self) -> None:
        snapshot = transition(_snapshot(), _decision("PROMOTE_READY", "d1", auditPassed=True, approvals=["A3", "A4"]))
        revoked = transition(snapshot, _decision("REVOKE", "d2", reason="key compromise"))
        self.assertEqual("REVOKED", revoked["state"])
        self.assertFalse(can_distribute(revoked["state"]))
        with self.assertRaises(ValueError):
            transition(revoked, _decision("RESUME", "d3", freshGo=True, approvals=["A3", "A4"]))

    def test_promotion_and_resume_require_authority_and_unique_decisions(self) -> None:
        with self.assertRaises(ValueError):
            transition(_snapshot(), _decision("PROMOTE_READY", "d1", auditPassed=False, approvals=["A3"]))
        snapshot = transition(_snapshot(), _decision("PROMOTE_READY", "d1", auditPassed=True, approvals=["A3", "A4"]))
        with self.assertRaises(ValueError):
            transition(snapshot, _decision("PROMOTE_READY", "d1", auditPassed=True, approvals=["A3", "A4"]))

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
