from __future__ import annotations

import argparse
import copy
import json
from datetime import datetime
from pathlib import Path
from typing import Any


STATES = {"FROZEN", "READY", "COHORT_ACTIVE", "DISTRIBUTION_PAUSED", "REVOKED", "CLOSED"}
DISTRIBUTABLE_STATES = {"READY", "COHORT_ACTIVE"}
APPROVAL_ROLES = {"A3", "A4", "A5", "A6"}


def can_distribute(state: str) -> bool:
    return state in DISTRIBUTABLE_STATES


def _time(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def validate_snapshot(snapshot: dict[str, Any]) -> None:
    if not isinstance(snapshot, dict) or snapshot.get("schemaVersion") != 1:
        raise ValueError("operation snapshot schemaVersion must be 1")
    if not isinstance(snapshot.get("candidateRootId"), str) or not snapshot["candidateRootId"]:
        raise ValueError("operation snapshot candidateRootId is required")
    if snapshot.get("state") not in STATES:
        raise ValueError("operation snapshot state is invalid")
    log = snapshot.get("decisionLog")
    if not isinstance(log, list):
        raise ValueError("operation snapshot decisionLog must be an array")
    ids: set[str] = set()
    for item in log:
        if not isinstance(item, dict) or not isinstance(item.get("decisionId"), str) or not item["decisionId"]:
            raise ValueError("every operation decision requires a decisionId")
        if item["decisionId"] in ids:
            raise ValueError("operation decisionId must be unique")
        ids.add(item["decisionId"])


def _decision_base(snapshot: dict[str, Any], decision: dict[str, Any]) -> None:
    if not isinstance(decision, dict) or decision.get("schemaVersion") != 1:
        raise ValueError("operation decision schemaVersion must be 1")
    for key in ("decisionId", "action", "candidateRootId", "actorRole", "createdAt"):
        if not decision.get(key):
            raise ValueError(f"operation decision {key} is required")
    if decision.get("candidateRootId") != snapshot["candidateRootId"]:
        raise ValueError("operation decision candidateRootId differs from snapshot")
    if decision.get("actorRole") not in APPROVAL_ROLES:
        raise ValueError("operation decision actorRole is invalid")
    if not _time(decision.get("createdAt")):
        raise ValueError("operation decision createdAt must be ISO-8601")
    if decision["decisionId"] in {item.get("decisionId") for item in snapshot["decisionLog"]}:
        raise ValueError("operation decisionId has already been recorded")


def _approved(roles: Any) -> bool:
    return isinstance(roles, list) and "A3" in roles and bool({"A4", "A6"} & set(roles))


def transition(snapshot: dict[str, Any], decision: dict[str, Any]) -> dict[str, Any]:
    validate_snapshot(snapshot)
    _decision_base(snapshot, decision)
    current = snapshot["state"]
    action = decision["action"]
    if current in {"REVOKED", "CLOSED"}:
        raise ValueError(f"{current} candidate cannot transition")
    next_state: str
    if action == "PROMOTE_READY":
        if current != "FROZEN" or decision.get("auditPassed") is not True or not _approved(decision.get("approvals")):
            raise ValueError("PROMOTE_READY requires a passing audit and A3 plus A4/A6 approval")
        next_state = "READY"
    elif action == "START_COHORT":
        if current != "READY" or not decision.get("consentVersion"):
            raise ValueError("START_COHORT requires READY and a consent version")
        next_state = "COHORT_ACTIVE"
    elif action == "PAUSE":
        if current not in {"READY", "COHORT_ACTIVE"} or not decision.get("reason"):
            raise ValueError("PAUSE requires a distributable state and reason")
        next_state = "DISTRIBUTION_PAUSED"
    elif action == "RESUME":
        if current != "DISTRIBUTION_PAUSED" or decision.get("freshGo") is not True or not _approved(decision.get("approvals")):
            raise ValueError("RESUME requires fresh GO and A3 plus A4/A6 approval")
        next_state = "COHORT_ACTIVE"
    elif action == "REVOKE":
        if not decision.get("reason"):
            raise ValueError("REVOKE requires a reason")
        next_state = "REVOKED"
    elif action == "CLOSE":
        if current != "COHORT_ACTIVE" or decision.get("cohortAuditPassed") is not True or decision.get("evaluationWindowEnded") is not True:
            raise ValueError("CLOSE requires active cohort, passing audit, and ended evaluation window")
        next_state = "CLOSED"
    else:
        raise ValueError(f"unsupported operation action: {action}")
    updated = copy.deepcopy(snapshot)
    updated["state"] = next_state
    updated["decisionLog"].append(copy.deepcopy(decision))
    return updated


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Apply one fail-closed External Beta operation decision")
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--decision", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = transition(load_json(args.snapshot), load_json(args.decision))
        payload = {"passed": True, "state": result["state"], "snapshot": result}
        exit_code = 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "errors": [str(exc)], "blocked": ["operation"]}
        exit_code = 0 if args.expect_blocked else 2
    text = json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
