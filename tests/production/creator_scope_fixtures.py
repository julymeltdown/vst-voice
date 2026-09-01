from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path

from tools.creator_scope.evidence import JsonObject, load_object


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_DIRECTORY = Path("docs/product/creator-beta")
RECORD_NAME = "creator-scope-ratification.json"


def copy_contract(root: Path) -> None:
    destination = root / CONTRACT_DIRECTORY
    _ = destination.mkdir(parents=True)
    for name in (
        "CREATOR_SCOPE_RATIFICATION.md",
        "creator-scope-ratification.schema.json",
        RECORD_NAME,
    ):
        _ = shutil.copy2(
            REPOSITORY_ROOT / CONTRACT_DIRECTORY / name, destination / name
        )


def evidence(root: Path, name: str) -> JsonObject:
    relative_path = Path("docs/evidence") / f"{name}.json"
    artifact = root / relative_path
    _ = artifact.parent.mkdir(parents=True, exist_ok=True)
    _ = artifact.write_text(json.dumps({"name": name}) + "\n", encoding="utf-8")
    return {
        "kind": name,
        "path": relative_path.as_posix(),
        "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        "capturedAt": "2026-09-01T00:00:00Z",
        "reviewerId": "evidence-reviewer",
    }


def record(root: Path) -> JsonObject:
    errors: list[str] = []
    payload = load_object(root / CONTRACT_DIRECTORY / RECORD_NAME, errors)
    assert payload is not None, errors
    return payload


def complete_record(root: Path) -> JsonObject:
    payload = record(root)
    payload.update(
        {"status": "PASS", "decision": "RATIFIED", "schema8Authorization": True}
    )

    prerequisites = payload["prerequisites"]
    assert isinstance(prerequisites, dict)
    for name, value in prerequisites.items():
        assert isinstance(name, str)
        assert isinstance(value, dict)
        artifact = evidence(root, f"prerequisite-{name}")
        value.update(
            {
                "status": "PASS",
                "locator": artifact["path"],
                "sha256": artifact["sha256"],
                "evidence": [artifact],
            }
        )

    hypotheses = payload["hypotheses"]
    assert isinstance(hypotheses, list)
    for value in hypotheses:
        assert isinstance(value, dict)
        hypothesis_id = value["id"]
        assert isinstance(hypothesis_id, str)
        value.update(
            {
                "status": "RATIFIED",
                "observedSessionIds": ["CSR-001", "CSR-002"],
                "evidence": [evidence(root, f"hypothesis-{hypothesis_id.lower()}")],
            }
        )

    sessions = payload["sessions"]
    assert isinstance(sessions, list)
    for index, value in enumerate(sessions, 1):
        assert isinstance(value, dict)
        completed = index <= 3
        value.update(
            {
                "participantId": f"creator-{index:02d}",
                "platform": "macos" if index % 2 else "windows",
                "status": "COMPLETED" if completed else "WITHDRAWN",
                "profile": {
                    "targetSegmentMatch": True,
                    "creatorType": "BOTH",
                    "usesDaw": True,
                    "openUtauExperienceYears": 1,
                },
                "consent": {
                    "status": "GRANTED" if completed else "WITHDRAWN",
                    "version": "1",
                    "evidence": [evidence(root, f"consent-{index}")],
                },
                "continuationDecision": "CONTINUE" if completed else "WITHDRAWN",
                "evidence": [evidence(root, f"session-{index}")],
            }
        )
        tasks = value["tasks"]
        assert isinstance(tasks, list)
        if completed:
            for task in tasks:
                assert isinstance(task, dict)
                hypothesis_id = task["hypothesisId"]
                assert isinstance(hypothesis_id, str)
                task.update(
                    {
                        "status": "COMPLETED",
                        "startedAt": "2026-09-01T00:00:00Z",
                        "endedAt": "2026-09-01T00:01:00Z",
                        "completionSeconds": 60,
                        "success": True,
                        "blockerSeverity": "NONE",
                        "observedPain": True,
                        "evidence": [
                            evidence(root, f"task-{index}-{hypothesis_id.lower()}")
                        ],
                    }
                )

    summary = payload["summary"]
    assert isinstance(summary, dict)
    summary.update(
        {
            "recruitedCount": 5,
            "completedSessionCount": 3,
            "continuationCount": 3,
            "ratifiedHypothesisCount": 6,
        }
    )

    approvals = payload["approvals"]
    assert isinstance(approvals, list)
    reviewer_ids = (
        "research-reviewer-one",
        "qa-reviewer-one",
        "engineering-reviewer-one",
    )
    for value, reviewer_id in zip(approvals, reviewer_ids, strict=True):
        assert isinstance(value, dict)
        value.update(
            {
                "status": "APPROVED",
                "reviewerId": reviewer_id,
                "decidedAt": "2026-09-01T00:00:00Z",
                "evidence": [evidence(root, f"approval-{reviewer_id}")],
            }
        )
    return payload


def write_record(root: Path, payload: JsonObject) -> None:
    _ = (root / CONTRACT_DIRECTORY / RECORD_NAME).write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
