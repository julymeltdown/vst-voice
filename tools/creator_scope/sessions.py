from __future__ import annotations

from pathlib import Path

from .contracts import HYPOTHESIS_IDS, PARTICIPANT_IDS, SESSION_IDS, SessionFacts
from .evidence import JsonObject, as_array, as_object, evidence_errors


def session_facts(root: Path, payload: JsonObject) -> SessionFacts:
    errors: list[str] = []
    sessions = as_array(payload.get("sessions"), "sessions", errors) or []
    slot_ids: list[str] = []
    participant_ids: list[str] = []
    completed_ids: set[str] = set()
    continuing = 0
    unresolved = {"P0": 0, "P1": 0, "P2": 0}
    for index, value in enumerate(sessions):
        session = as_object(value, f"sessions[{index}]", errors)
        if session is None:
            continue
        slot_id = session.get("slotId")
        participant_id = session.get("participantId")
        if isinstance(slot_id, str):
            slot_ids.append(slot_id)
        else:
            errors.append(f"sessions[{index}].slotId is required")
            continue
        if isinstance(participant_id, str):
            participant_ids.append(participant_id)
        else:
            errors.append(f"{slot_id}.participantId is required for PASS")
        status = session.get("status")
        if status not in ("COMPLETED", "WITHDRAWN"):
            errors.append(f"{slot_id}.status must be COMPLETED or WITHDRAWN")
        profile = as_object(session.get("profile"), f"{slot_id}.profile", errors)
        consent = as_object(session.get("consent"), f"{slot_id}.consent", errors)
        if consent is not None:
            if consent.get("status") not in ("GRANTED", "WITHDRAWN"):
                errors.append(f"{slot_id}.consent.status must be GRANTED or WITHDRAWN")
            errors.extend(
                evidence_errors(
                    root, consent.get("evidence"), f"{slot_id}.consent.evidence"
                )
            )
        if status == "COMPLETED":
            if consent is None or consent.get("status") != "GRANTED":
                errors.append(f"{slot_id}.consent.status must be GRANTED")
            if profile is None or profile.get("targetSegmentMatch") is not True:
                errors.append(f"{slot_id}.profile must match the target segment")
            if profile is None or profile.get("usesDaw") is not True:
                errors.append(f"{slot_id}.profile.usesDaw must be true")
            completed_ids.add(slot_id)
            tasks = as_array(session.get("tasks"), f"{slot_id}.tasks", errors) or []
            task_ids = [
                task.get("hypothesisId") for task in tasks if isinstance(task, dict)
            ]
            if tuple(task_ids) != HYPOTHESIS_IDS:
                errors.append(
                    f"{slot_id}.tasks must cover {list(HYPOTHESIS_IDS)} in order"
                )
            for task_index, task_value in enumerate(tasks):
                task = as_object(task_value, f"{slot_id}.tasks[{task_index}]", errors)
                if task is None:
                    continue
                if task.get("status") != "COMPLETED":
                    errors.append(f"{slot_id}.tasks[{task_index}] must be COMPLETED")
                blocker = task.get("blockerSeverity")
                if blocker in ("P0", "P1"):
                    errors.append(
                        f"{slot_id}.tasks[{task_index}] retains an unresolved {blocker} blocker"
                    )
                errors.extend(
                    evidence_errors(
                        root,
                        task.get("evidence"),
                        f"{slot_id}.tasks[{task_index}].evidence",
                    )
                )
            if session.get("continuationDecision") == "CONTINUE":
                continuing += 1
        errors.extend(
            evidence_errors(root, session.get("evidence"), f"{slot_id}.evidence")
        )
        issues = as_array(session.get("issues"), f"{slot_id}.issues", errors) or []
        for issue_value in issues:
            issue = as_object(issue_value, f"{slot_id}.issue", errors)
            if issue is None or issue.get("status") == "RESOLVED":
                continue
            severity = issue.get("severity")
            if severity in unresolved:
                unresolved[str(severity)] += 1
    if tuple(slot_ids) != SESSION_IDS:
        errors.append(f"session slots must be exactly {list(SESSION_IDS)}")
    if tuple(participant_ids) != PARTICIPANT_IDS:
        errors.append(f"participant IDs must be exactly {list(PARTICIPANT_IDS)}")
    return SessionFacts(
        recruited=len(participant_ids),
        completed=len(completed_ids),
        continuing=continuing,
        unresolved_p0=unresolved["P0"],
        unresolved_p1=unresolved["P1"],
        unresolved_p2=unresolved["P2"],
        completed_ids=frozenset(completed_ids),
        errors=tuple(errors),
    )
