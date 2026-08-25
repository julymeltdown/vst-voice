from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from tools.engineering_remediation_artifacts import validate_artifact
from tools.engineering_remediation_contract import (
    JsonObject,
    JsonValue,
    REQUIRED_GATES,
    REQUIRED_MANUAL_QA,
    REQUIRED_REVIEW_LANES,
    SHA40,
    SHA256,
)


class EvidenceInputError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class EvidenceValidationResult:
    passed: bool
    errors: tuple[str, ...]


def _object(value: JsonValue, label: str, errors: list[str]) -> JsonObject | None:
    if not isinstance(value, dict) or not all(
        isinstance(key, str) for key in value
    ):
        errors.append(f"{label} must be an object")
        return None
    return value


def _sha(
    value: JsonValue, label: str, pattern: re.Pattern[str], errors: list[str]
) -> str | None:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        errors.append(f"{label} must be a full lowercase hexadecimal digest")
        return None
    if set(value) == {"0"}:
        errors.append(f"{label} must not be a placeholder digest")
        return None
    return value


def _time(value: JsonValue, label: str, errors: list[str]) -> datetime | None:
    if not isinstance(value, str):
        errors.append(f"{label} must be an ISO-8601 timestamp")
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        errors.append(f"{label} must be an ISO-8601 timestamp")
        return None


def _entries(value: JsonValue, label: str, errors: list[str]) -> list[JsonObject]:
    if not isinstance(value, list):
        errors.append(f"{label} must be an array")
        return []
    entries: list[JsonObject] = []
    for index, item in enumerate(value):
        entry = _object(item, f"{label}[{index}]", errors)
        if entry is not None:
            entries.append(entry)
    return entries


def _exact_ids(
    entries: list[JsonObject], expected: tuple[str, ...], label: str,
    errors: list[str]
) -> None:
    raw_identifiers = [entry.get("id") for entry in entries]
    if not all(isinstance(identifier, str) for identifier in raw_identifiers):
        errors.append(f"{label} IDs must be strings")
    identifiers = [
        identifier for identifier in raw_identifiers if isinstance(identifier, str)
    ]
    if len(set(identifiers)) != len(identifiers):
        errors.append(f"{label} contains duplicate IDs")
    if set(identifiers) != set(expected):
        errors.append(f"{label} set does not match the required contract")


def validate_evidence(
    record: JsonObject,
    evidence_root: Path | None = None,
    containing_commit_sha: str | None = None,
) -> EvidenceValidationResult:
    errors: list[str] = []
    if record.get("schemaVersion") != 1:
        errors.append("schemaVersion must be 1")
    if record.get("recordKind") != "ENGINEERING_REMEDIATION_EVIDENCE":
        errors.append("recordKind is invalid")
    if record.get("recordStatus") != "SEALED":
        errors.append("recordStatus must be SEALED")
    base = _sha(record.get("reviewBaseSha"), "reviewBaseSha", SHA40, errors)
    candidate = _sha(record.get("candidateSha"), "candidateSha", SHA40, errors)
    if base is not None and base == candidate:
        errors.append("reviewBaseSha must differ from candidateSha")
    if record.get("candidateTreeState") != "CLEAN":
        errors.append("candidateTreeState must be CLEAN")
    _time(record.get("generatedAt"), "generatedAt", errors)

    attestation = record.get("attestationCommitSha")
    if attestation is not None:
        attestation = _sha(attestation, "attestationCommitSha", SHA40, errors)
        if attestation is not None and attestation == candidate:
            errors.append("attestation commit must not equal the candidate")
    if containing_commit_sha is not None:
        containing = _sha(
            containing_commit_sha, "containingCommitSha", SHA40, errors
        )
        if containing is not None and containing == candidate:
            errors.append("evidence cannot contain itself in the candidate commit")
        if attestation is not None and containing != attestation:
            errors.append("attestationCommitSha does not match the containing commit")

    environment = _object(record.get("environment"), "environment", errors)
    if environment is not None:
        for field in (
            "platform", "architecture", "compiler", "cmake", "python", "generator"
        ):
            if not isinstance(environment.get(field), str) or not environment[field]:
                errors.append(f"environment.{field} is required")
        _sha(
            environment.get("redactedEnvironmentId"),
            "environment.redactedEnvironmentId",
            SHA256,
            errors,
        )

    gates = _entries(record.get("gates"), "gates", errors)
    _exact_ids(gates, REQUIRED_GATES, "gate set", errors)
    for index, gate in enumerate(gates):
        label = f"gates[{index}]"
        if gate.get("candidateSha") != candidate:
            errors.append(f"{label} belongs to another candidate")
        if gate.get("status") != "PASS":
            errors.append(f"{label}.status must be PASS")
        if not isinstance(gate.get("command"), str) or not gate["command"]:
            errors.append(f"{label}.command is required")
        exit_status = gate.get("exitStatus")
        if (
            not isinstance(exit_status, int)
            or isinstance(exit_status, bool)
            or exit_status != 0
        ):
            errors.append(f"{label}.exitStatus must be zero")
        started = _time(gate.get("startedAt"), f"{label}.startedAt", errors)
        ended = _time(gate.get("endedAt"), f"{label}.endedAt", errors)
        if started is not None and ended is not None and ended < started:
            errors.append(f"{label} ends before it starts")
        validate_artifact(
            gate.get("rawLog"), f"{label}.rawLog", evidence_root, errors
        )

    journeys = _entries(record.get("manualQa"), "manualQa", errors)
    _exact_ids(journeys, REQUIRED_MANUAL_QA, "manual QA set", errors)
    for index, journey in enumerate(journeys):
        if journey.get("status") != "PASS":
            errors.append(f"manualQa[{index}].status must be PASS")
        if journey.get("evidenceGate") not in REQUIRED_GATES:
            errors.append(f"manualQa[{index}].evidenceGate is invalid")

    review = _object(record.get("review"), "review", errors)
    if review is not None:
        if review.get("baseSha") != base or review.get("candidateSha") != candidate:
            errors.append("review does not bind the base and candidate")
        counts = (review.get("p0Count"), review.get("p1Count"))
        if any(
            not isinstance(count, int) or isinstance(count, bool) or count != 0
            for count in counts
        ):
            errors.append("review must report zero current P0/P1 findings")
        lanes = _entries(review.get("lanes"), "review.lanes", errors)
        _exact_ids(lanes, REQUIRED_REVIEW_LANES, "review lane set", errors)
        for index, lane in enumerate(lanes):
            if lane.get("candidateSha") != candidate:
                errors.append(f"review.lanes[{index}] belongs to another candidate")
            if lane.get("verdict") != "PASS":
                errors.append(f"review.lanes[{index}].verdict must be PASS")
            validate_artifact(
                lane.get("report"),
                f"review.lanes[{index}].report",
                evidence_root,
                errors,
            )

    archive = _object(record.get("evidenceArchive"), "evidenceArchive", errors)
    if archive is not None:
        if archive.get("sealed") is not True:
            errors.append("evidenceArchive.sealed must be true")
        validate_artifact(archive, "evidenceArchive", evidence_root, errors)
    if record.get("externalReleaseState") != "BLOCKED":
        errors.append("externalReleaseState must remain BLOCKED")
    return EvidenceValidationResult(not errors, tuple(errors))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path)
    parser.add_argument("--containing-commit-sha")
    args = parser.parse_args(argv)
    try:
        value = json.loads(args.record.read_text(encoding="utf-8"))
        if not isinstance(value, dict):
            raise EvidenceInputError("evidence record root must be an object")
        result = validate_evidence(
            value, args.evidence_root, args.containing_commit_sha
        )
    except (OSError, UnicodeError, json.JSONDecodeError, EvidenceInputError) as error:
        print(f"ENGINEERING_EVIDENCE=ERROR {error}")
        return 2
    if result.passed:
        print("ENGINEERING_EVIDENCE=PASS")
        return 0
    print(f"ENGINEERING_EVIDENCE=FAIL errors={len(result.errors)}")
    for error in result.errors:
        print(error)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
