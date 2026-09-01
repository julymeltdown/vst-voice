from __future__ import annotations

from pathlib import Path
from collections.abc import Callable
from typing import Final

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import SchemaError

from .contracts import (
    APPROVAL_ROLES,
    CANONICAL_DOCUMENT,
    HYPOTHESIS_IDS,
    OPENUTAU_COMMIT,
    PREREQUISITE_IDS,
    RECORD_PATH,
    REVIEWER_PREFIXES,
    SCHEMA_PATH,
    RecordStatus,
    ScopeVerification,
)

from .evidence import (
    JsonObject,
    JsonValue,
    artifact_errors as _artifact_errors,
    as_array as _array,
    as_object as _object,
    evidence_errors as _evidence_errors,
    load_object as _load_object,
)
from .sessions import session_facts as _session_facts


STATUS_BY_VALUE: Final = {status.value: status for status in RecordStatus}


def _parse_status(value: JsonValue) -> RecordStatus | None:
    return STATUS_BY_VALUE.get(value) if isinstance(value, str) else None


def _schema_errors(schema: JsonObject, payload: JsonObject) -> tuple[str, ...]:
    try:
        Draft202012Validator.check_schema(schema)
    except SchemaError as error:
        return (f"creator scope schema is invalid: {error.message}",)
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    violations = sorted(
        validator.iter_errors(payload), key=lambda error: error.json_path
    )
    return tuple(
        f"record schema violation at {error.json_path}: {error.message}"
        for error in violations
    )


def _pass_errors(root: Path, payload: JsonObject) -> tuple[str, ...]:
    errors: list[str] = []
    if payload.get("decision") != "RATIFIED":
        errors.append("PASS requires decision RATIFIED")
    if payload.get("schema8Authorization") is not True:
        errors.append("PASS requires schema8Authorization true")
    prerequisites = _object(payload.get("prerequisites"), "prerequisites", errors)
    if prerequisites is not None:
        if set(prerequisites) != set(PREREQUISITE_IDS):
            errors.append(f"prerequisites must be exactly {list(PREREQUISITE_IDS)}")
        for name in PREREQUISITE_IDS:
            item = _object(prerequisites.get(name), f"prerequisites.{name}", errors)
            if item is None:
                continue
            if item.get("status") != "PASS":
                errors.append(f"prerequisites.{name}.status must be PASS")
            if (
                name == "openUtauReference"
                and item.get("sourceCommit") != OPENUTAU_COMMIT
            ):
                errors.append(
                    f"prerequisites.openUtauReference.sourceCommit must be {OPENUTAU_COMMIT}"
                )
            errors.extend(
                _artifact_errors(
                    root,
                    {"path": item.get("locator"), "sha256": item.get("sha256")},
                    f"prerequisites.{name}",
                )
            )
            errors.extend(
                _evidence_errors(
                    root, item.get("evidence"), f"prerequisites.{name}.evidence"
                )
            )
    facts = _session_facts(root, payload)
    errors.extend(facts.errors)
    hypotheses = _array(payload.get("hypotheses"), "hypotheses", errors) or []
    ratified = 0
    hypothesis_ids: list[str] = []
    for index, value in enumerate(hypotheses):
        hypothesis = _object(value, f"hypotheses[{index}]", errors)
        if hypothesis is None:
            continue
        hypothesis_id = hypothesis.get("id")
        if isinstance(hypothesis_id, str):
            hypothesis_ids.append(hypothesis_id)
        else:
            errors.append(f"hypotheses[{index}].id is required")
        if hypothesis.get("status") == "RATIFIED":
            ratified += 1
        else:
            errors.append(f"hypotheses[{index}].status must be RATIFIED")
        observed = (
            _array(
                hypothesis.get("observedSessionIds"),
                f"hypotheses[{index}].observedSessionIds",
                errors,
            )
            or []
        )
        observed_ids = {item for item in observed if isinstance(item, str)}
        if len(observed_ids) < 2 or not observed_ids.issubset(facts.completed_ids):
            errors.append(
                f"hypotheses[{index}] must cite at least two completed sessions"
            )
        errors.extend(
            _evidence_errors(
                root, hypothesis.get("evidence"), f"hypotheses[{index}].evidence"
            )
        )
    if tuple(hypothesis_ids) != HYPOTHESIS_IDS:
        errors.append(f"hypotheses must be exactly {list(HYPOTHESIS_IDS)}")
    approvals = _array(payload.get("approvals"), "approvals", errors) or []
    roles: list[str] = []
    reviewers: list[str] = []
    for index, value in enumerate(approvals):
        approval = _object(value, f"approvals[{index}]", errors)
        if approval is None:
            continue
        role = approval.get("role")
        reviewer = approval.get("reviewerId")
        if isinstance(role, str) and isinstance(reviewer, str):
            roles.append(role)
            reviewers.append(reviewer)
            prefix = REVIEWER_PREFIXES.get(role)
            if prefix is None or not reviewer.startswith(prefix):
                errors.append(
                    f"approvals[{index}].reviewerId does not match role {role}"
                )
        else:
            errors.append(f"approvals[{index}] requires role and reviewerId")
        if approval.get("status") != "APPROVED":
            errors.append(f"approvals[{index}].status must be APPROVED")
        errors.extend(
            _evidence_errors(
                root, approval.get("evidence"), f"approvals[{index}].evidence"
            )
        )
    if tuple(roles) != APPROVAL_ROLES:
        errors.append(f"approval roles must be exactly {list(APPROVAL_ROLES)}")
    if len(set(reviewers)) != len(reviewers):
        errors.append("approval reviewer IDs must be distinct")
    summary = _object(payload.get("summary"), "summary", errors)
    if summary is not None:
        expected = {
            "recruitedCount": facts.recruited,
            "completedSessionCount": facts.completed,
            "continuationCount": facts.continuing,
            "ratifiedHypothesisCount": ratified,
            "unresolvedP0Count": facts.unresolved_p0,
            "unresolvedP1Count": facts.unresolved_p1,
            "unresolvedP2Count": facts.unresolved_p2,
        }
        for name, count in expected.items():
            if summary.get(name) != count:
                errors.append(f"summary.{name} must equal observed count {count}")
        if (
            summary.get("planAmendmentRequired") is not False
            or summary.get("planAmendmentLocator") is not None
        ):
            errors.append("PASS cannot require a plan amendment")
    if facts.recruited != 5 or facts.completed < 3 or facts.continuing < 3:
        errors.append(
            "PASS requires five recruited, three completed, and three continuing creators"
        )
    if facts.unresolved_p0 or facts.unresolved_p1:
        errors.append("PASS cannot contain unresolved P0/P1 issues")
    return tuple(errors)


def _not_run_errors(_root: Path, payload: JsonObject) -> tuple[str, ...]:
    if (
        payload.get("decision") == "PENDING"
        and payload.get("schema8Authorization") is False
    ):
        return ()
    return ("NOT_RUN must remain PENDING with schema8Authorization false",)


def _blocked_errors(_root: Path, payload: JsonObject) -> tuple[str, ...]:
    if payload.get("schema8Authorization") is False:
        return ()
    return ("BLOCKED must keep schema8Authorization false",)


StatusVerifier = Callable[[Path, JsonObject], tuple[str, ...]]
STATUS_VERIFIERS: Final[dict[RecordStatus, StatusVerifier]] = {
    RecordStatus.NOT_RUN: _not_run_errors,
    RecordStatus.BLOCKED: _blocked_errors,
    RecordStatus.PASS: _pass_errors,
}


def verify_repository(root: Path) -> ScopeVerification:
    repository = root.resolve()
    errors: list[str] = []
    if not (repository / CANONICAL_DOCUMENT).is_file():
        errors.append(f"missing canonical contract: {CANONICAL_DOCUMENT.as_posix()}")
    schema = _load_object(repository / SCHEMA_PATH, errors)
    payload = _load_object(repository / RECORD_PATH, errors)
    if schema is None or payload is None:
        return ScopeVerification("INVALID", False, tuple(errors))
    errors.extend(_schema_errors(schema, payload))
    if (
        payload.get("schemaVersion") != 1
        or payload.get("recordType") != "creator-scope-ratification"
    ):
        errors.append(
            "record identity must be creator-scope-ratification schema version 1"
        )
    status = _parse_status(payload.get("status"))
    if status is None:
        errors.append("status must be NOT_RUN, BLOCKED, or PASS")
        return ScopeVerification("INVALID", False, tuple(errors))
    errors.extend(STATUS_VERIFIERS[status](repository, payload))
    state = status.value
    authorized = status is RecordStatus.PASS and not errors
    return ScopeVerification(state, authorized, tuple(errors))
