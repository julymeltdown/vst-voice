from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.cohort_gate import validate_cohort

ROOT = Path(__file__).resolve().parents[2]


def _cohort() -> dict:
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-cohort",
        "status": "PASS",
        "decision": "CLOSED",
        "candidateRootId": "candidate-root-001",
        "evaluationWindow": {"status": "ENDED", "startedAt": "2026-08-20T00:00:00Z", "endedAt": "2026-08-22T00:00:00Z"},
        "consent": {"version": "consent-1", "scope": "external-beta", "retention": "technical-180d-private-30d", "registrySha256": "a" * 64},
        "assignments": [
            {"participantId": "participant-a1", "platform": "macos", "status": "COMPLETED", "reason": "finished"},
            {"participantId": "participant-b1", "platform": "windows", "status": "COMPLETED", "reason": "finished"},
        ],
        "externalSessions": [
            {"participantId": "participant-a1", "platform": "macos", "status": "COMPLETED", "flows": ["F1", "F2", "F5"]},
            {"participantId": "participant-b1", "platform": "windows", "status": "COMPLETED", "flows": ["F1", "F2", "F5"]},
        ],
        "claimedHostTuples": ["macos/reaper/7.30/CLAP", "windows/reaper/7.30/CLAP"],
        "hostSessions": [
            {"tuple": "macos/reaper/7.30/CLAP", "participantId": "participant-a1", "status": "COMPLETED"},
            {"tuple": "windows/reaper/7.30/CLAP", "participantId": "participant-b1", "status": "COMPLETED"},
        ],
        "checkIns": [
            {"id": "initial", "participantId": "participant-a1", "kind": "INITIAL", "status": "RECORDED", "at": "2026-08-20T01:00:00Z", "evidenceRecordId": "checkin-1"},
            {"id": "hour-1", "participantId": "participant-a1", "kind": "PLUS_1_HOUR", "status": "RECORDED", "at": "2026-08-20T02:00:00Z", "evidenceRecordId": "checkin-2"},
            {"id": "day-1", "participantId": "participant-a1", "kind": "PLUS_24_HOURS", "status": "RECORDED", "at": "2026-08-21T01:00:00Z", "evidenceRecordId": "checkin-3"},
            {"id": "closure", "participantId": "participant-a1", "kind": "CLOSURE", "status": "RECORDED", "at": "2026-08-22T00:00:00Z", "evidenceRecordId": "checkin-4"},
        ],
        "checkpoints": [{"id": "cp-1", "status": "RESOLVED"}],
        "incidents": [],
        "approvals": [{"role": "A3", "status": "APPROVED"}, {"role": "A4", "status": "APPROVED"}],
    }


class CohortGateTests(unittest.TestCase):
    def test_complete_closed_cohort_passes(self) -> None:
        result = validate_cohort(_cohort(), "CLOSED")
        self.assertTrue(result.passed, result.errors)

    def test_missing_platform_coverage_and_open_incident_block(self) -> None:
        cohort = _cohort()
        cohort["externalSessions"] = cohort["externalSessions"][:1]
        cohort["incidents"] = [{"id": "incident-1", "severity": "Blocker", "status": "OPEN"}]
        result = validate_cohort(cohort, "CLOSED")
        self.assertFalse(result.passed)
        self.assertTrue(any("windows" in error for error in result.errors))
        self.assertTrue(any("Blocker" in error for error in result.errors))

    def test_pii_and_nonterminal_assignment_are_rejected(self) -> None:
        cohort = copy.deepcopy(_cohort())
        cohort["assignments"][0]["email"] = "person@example.com"
        cohort["assignments"][1]["status"] = "IN_PROGRESS"
        result = validate_cohort(cohort, "CLOSED")
        self.assertFalse(result.passed)
        self.assertTrue(any("PII" in error for error in result.errors))
        self.assertTrue(any("terminal" in error for error in result.errors))

    def test_cli_expect_blocked_is_observable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cohort.json"
            cohort = _cohort()
            cohort["externalSessions"] = []
            path.write_text(json.dumps(cohort), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(ROOT / "scripts/run_external_beta_cohort_gate.py"), "--cohort", str(path), "--state", "CLOSED", "--expect-blocked"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            self.assertIn('"passed": false', completed.stdout)


if __name__ == "__main__":
    unittest.main()
