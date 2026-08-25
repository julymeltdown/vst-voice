from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PARTICIPANT_ID = re.compile(r"^participant-[a-z0-9-]+$")
PLATFORMS = {"macos", "windows"}
TERMINAL_ASSIGNMENTS = {"COMPLETED", "WITHDRAWN", "DISQUALIFIED"}
PII_KEYS = {"email", "name", "fullName", "realName", "phone", "address", "contact"}
REQUIRED_FLOWS = {"F1", "F2", "F5"}
REQUIRED_CHECKINS = {"INITIAL", "PLUS_1_HOUR", "PLUS_24_HOURS", "CLOSURE"}


@dataclass(frozen=True, slots=True)
class CohortResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors), "blocked": list(self.blocked)}


def _contains_pii(value: Any) -> bool:
    if isinstance(value, dict):
        return any(key in PII_KEYS or _contains_pii(item) for key, item in value.items())
    if isinstance(value, list):
        return any(_contains_pii(item) for item in value)
    return False


def validate_cohort(cohort: dict[str, Any], state: str = "CLOSED") -> CohortResult:
    errors: list[str] = []
    blocked: list[str] = []
    if not isinstance(cohort, dict):
        return CohortResult(False, ("cohort must be an object",), ())
    if cohort.get("schemaVersion") != 1:
        errors.append("cohort.schemaVersion must be 1")
    if cohort.get("recordType") != "external-beta-cohort":
        errors.append("cohort.recordType is invalid")
    if cohort.get("status") != "PASS":
        errors.append("cohort.status must be PASS")
        blocked.append("cohort")
    if cohort.get("decision") != state:
        errors.append(f"cohort.decision must be {state}")
    if not isinstance(cohort.get("candidateRootId"), str) or not cohort["candidateRootId"]:
        errors.append("cohort.candidateRootId is required")
    window = cohort.get("evaluationWindow")
    if not isinstance(window, dict):
        errors.append("evaluationWindow must be an object")
    elif state == "CLOSED" and window.get("status") != "ENDED":
        errors.append("evaluationWindow must be ENDED for CLOSED")
    elif state != "CLOSED" and window.get("status") not in {"OPEN", "ENDED"}:
        errors.append("evaluationWindow must be OPEN or ENDED")
    consent = cohort.get("consent")
    if not isinstance(consent, dict) or not consent.get("version") or not consent.get("scope") or not consent.get("retention") or not isinstance(consent.get("registrySha256"), str) or len(consent["registrySha256"]) != 64:
        errors.append("consent must include version, scope, retention, and registrySha256")
    if _contains_pii(cohort.get("assignments", [])):
        errors.append("cohort assignments contain forbidden PII")
    assignments = cohort.get("assignments")
    assignment_ids: set[str] = set()
    if not isinstance(assignments, list) or not assignments:
        errors.append("assignments must be non-empty")
        assignments = []
    for index, assignment in enumerate(assignments):
        label = f"assignments[{index}]"
        if not isinstance(assignment, dict):
            errors.append(f"{label} must be an object")
            continue
        participant = assignment.get("participantId")
        if not isinstance(participant, str) or PARTICIPANT_ID.fullmatch(participant) is None:
            errors.append(f"{label}.participantId must be pseudonymous")
        else:
            if participant in assignment_ids:
                errors.append(f"duplicate assignment: {participant}")
            assignment_ids.add(participant)
        if assignment.get("platform") not in PLATFORMS:
            errors.append(f"{label}.platform is invalid")
        if state == "CLOSED" and assignment.get("status") not in TERMINAL_ASSIGNMENTS:
            errors.append(f"{label} must have a terminal assignment status")
        if not isinstance(assignment.get("reason"), str) or not assignment["reason"]:
            errors.append(f"{label}.reason is required")
    sessions = cohort.get("externalSessions")
    completed_platforms: set[str] = set()
    if not isinstance(sessions, list):
        errors.append("externalSessions must be an array")
        sessions = []
    for session in sessions:
        if not isinstance(session, dict):
            errors.append("external session must be an object")
            continue
        if session.get("status") == "COMPLETED" and REQUIRED_FLOWS.issubset(set(session.get("flows", []))):
            completed_platforms.add(session.get("platform"))
        if session.get("participantId") not in assignment_ids:
            errors.append("external session references an unknown participant")
    for platform in sorted(PLATFORMS - completed_platforms):
        errors.append(f"cohort requires completed F1/F2/F5 session on {platform}")
        blocked.append(platform)
    claimed = cohort.get("claimedHostTuples")
    host_sessions = cohort.get("hostSessions")
    completed_hosts = {item.get("tuple") for item in host_sessions if isinstance(item, dict) and item.get("status") == "COMPLETED"} if isinstance(host_sessions, list) else set()
    if not isinstance(claimed, list) or any(item not in completed_hosts for item in claimed):
        errors.append("every claimed host tuple requires a completed external session")
        blocked.append("hosts")
    checkins = cohort.get("checkIns")
    checkin_kinds = {item.get("kind") for item in checkins if isinstance(item, dict) and item.get("status") == "RECORDED"} if isinstance(checkins, list) else set()
    if state == "CLOSED" and not REQUIRED_CHECKINS.issubset(checkin_kinds):
        errors.append("CLOSED cohort requires initial, +1 hour, +24 hour, and closure check-ins")
        blocked.append("check-ins")
    checkpoints = cohort.get("checkpoints")
    if not isinstance(checkpoints, list) or any(not isinstance(item, dict) or item.get("status") not in {"RESOLVED", "COMPLETED"} for item in checkpoints):
        errors.append("all cohort checkpoints must be terminal")
    incidents = cohort.get("incidents")
    if not isinstance(incidents, list):
        errors.append("incidents must be an array")
        incidents = []
    for incident in incidents:
        if not isinstance(incident, dict) or incident.get("status") not in {"RESOLVED", "CLOSED"}:
            errors.append("all incidents must be resolved or closed")
        if isinstance(incident, dict) and incident.get("severity") in {"Blocker", "Critical"} and incident.get("status") not in {"RESOLVED", "CLOSED"}:
            errors.append(f"unresolved {incident['severity']} incident blocks cohort")
            blocked.append("incident")
    approvals = cohort.get("approvals")
    approved_roles = {item.get("role") for item in approvals if isinstance(item, dict) and item.get("status") == "APPROVED"} if isinstance(approvals, list) else set()
    if "A3" not in approved_roles or not ({"A4", "A6"} & approved_roles):
        errors.append("cohort requires A3 and A4-or-A6 approvals")
        blocked.append("approvals")
    return CohortResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate External Beta cohort readiness or closure")
    parser.add_argument("--cohort", type=Path, required=True)
    parser.add_argument("--state", choices=("READY", "CLOSED"), default="CLOSED")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = validate_cohort(load_json(args.cohort), args.state)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "errors": [str(exc)], "blocked": []}
        print(json.dumps(payload, sort_keys=True))
        return 0 if args.expect_blocked else 2
    text = json.dumps(result.as_dict(), ensure_ascii=False, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
