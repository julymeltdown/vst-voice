from __future__ import annotations

from tools.external_beta import release_gate


def closed_cohort() -> release_gate.JsonObject:
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-cohort",
        "status": "PASS",
        "decision": "CLOSED",
        "candidateRootId": "candidate-root-001",
        "evaluationWindow": {
            "status": "ENDED",
            "endedAt": "2026-08-21T18:00:00Z",
            "startedAt": "2026-08-20T18:00:00Z",
        },
        "consent": {
            "version": "consent-1",
            "scope": "external-beta",
            "retention": "technical-180d-private-30d",
            "registrySha256": "9" * 64,
        },
        "externalSessions": [
            {
                "participantId": "participant-p1",
                "platform": "macos",
                "status": "COMPLETED",
                "flows": ["F1", "F2", "F5"],
            },
            {
                "participantId": "participant-p2",
                "platform": "windows",
                "status": "COMPLETED",
                "flows": ["F1", "F2", "F5"],
            },
        ],
        "claimedHostTuples": [
            "macos/reaper/7.0/CLAP",
            "windows/reaper/7.0/CLAP",
        ],
        "hostSessions": [
            {
                "tuple": "macos/reaper/7.0/CLAP",
                "participantId": "participant-p1",
                "status": "COMPLETED",
            },
            {
                "tuple": "windows/reaper/7.0/CLAP",
                "participantId": "participant-p2",
                "status": "COMPLETED",
            },
        ],
        "assignments": [
            {
                "participantId": "participant-p1",
                "platform": "macos",
                "status": "COMPLETED",
                "reason": "finished",
            },
            {
                "participantId": "participant-p2",
                "platform": "windows",
                "status": "COMPLETED",
                "reason": "finished",
            },
        ],
        "checkIns": [
            {
                "id": "initial",
                "participantId": "participant-p1",
                "kind": "INITIAL",
                "status": "RECORDED",
                "at": "2026-08-20T19:00:00Z",
                "evidenceRecordId": "checkin-1",
            },
            {
                "id": "hour",
                "participantId": "participant-p1",
                "kind": "PLUS_1_HOUR",
                "status": "RECORDED",
                "at": "2026-08-20T20:00:00Z",
                "evidenceRecordId": "checkin-2",
            },
            {
                "id": "day",
                "participantId": "participant-p1",
                "kind": "PLUS_24_HOURS",
                "status": "RECORDED",
                "at": "2026-08-21T19:00:00Z",
                "evidenceRecordId": "checkin-3",
            },
            {
                "id": "closure",
                "participantId": "participant-p1",
                "kind": "CLOSURE",
                "status": "RECORDED",
                "at": "2026-08-21T18:00:00Z",
                "evidenceRecordId": "checkin-4",
            },
        ],
        "checkpoints": [{"id": "cp1", "status": "RESOLVED"}],
        "incidents": [],
        "approvals": [
            {"role": "A3", "status": "APPROVED"},
            {"role": "A4", "status": "APPROVED"},
        ],
    }
