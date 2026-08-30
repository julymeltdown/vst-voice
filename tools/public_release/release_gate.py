from __future__ import annotations

import argparse
import json
from pathlib import Path

from .approval_validation import approval_findings
from .candidate_validation import candidate_findings
from .contracts import (
    PUBLIC_REQUIREMENT_IDS,
    PUBLIC_STATES,
    REQUIRED_APPROVAL_ROLES,
    GateResult,
    JsonObject,
    JsonValue,
    ReleaseGateInputError,
    ValidationFinding,
    approval_envelope_sha256,
    canonical_json,
    operation_decision_sha256,
    operation_envelope_sha256,
    root_sha256,
    sha256_json,
)
from .evidence_validation import evidence_findings
from .operations import transition


__all__ = (
    "PUBLIC_REQUIREMENT_IDS",
    "PUBLIC_STATES",
    "REQUIRED_APPROVAL_ROLES",
    "GateResult",
    "JsonObject",
    "JsonValue",
    "ReleaseGateInputError",
    "approval_envelope_sha256",
    "canonical_json",
    "evaluate_gate",
    "evaluate_public_active",
    "operation_decision_sha256",
    "operation_envelope_sha256",
    "root_sha256",
    "sha256_json",
    "transition",
)


def load_json(path: Path) -> JsonObject:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ReleaseGateInputError(f"JSON root must be an object: {path}")
    return value


def load_acceptance_contract() -> JsonObject:
    return load_json(
        Path(__file__).resolve().parents[2]
        / "docs/product/public-release-acceptance.json"
    )


def evaluate_gate(
    candidate: JsonObject,
    state: str = "PUBLIC_ACTIVE",
    acceptance_contract: JsonObject | None = None,
    *,
    archive_verified: bool = False,
) -> GateResult:
    normalized = state.upper().replace(" ", "_")
    if normalized not in PUBLIC_STATES:
        return GateResult(
            normalized,
            False,
            (f"unsupported public release state: {state}",),
        )
    contract = (
        acceptance_contract
        if acceptance_contract is not None
        else load_acceptance_contract()
    )
    findings = [
        *candidate_findings(candidate, contract),
        *evidence_findings(candidate),
        *approval_findings(candidate, contract),
    ]
    if candidate.get("state") != normalized:
        findings.append(
            ValidationFinding(
                "PR-001-contract",
                f"candidate state must equal {normalized}",
            )
        )
    if normalized == "PUBLIC_ACTIVE" and not archive_verified:
        findings.append(
            ValidationFinding(
                "PR-012-archive-restore",
                "verified restored archive audit is required for PUBLIC_ACTIVE",
            )
        )
    blocked = tuple(
        requirement_id
        for requirement_id in PUBLIC_REQUIREMENT_IDS
        if any(finding.requirement_id == requirement_id for finding in findings)
    )
    errors = tuple(finding.message for finding in findings)
    return GateResult(normalized, not errors and not blocked, errors, blocked)


def evaluate_public_active(
    candidate: JsonObject,
    acceptance_contract: JsonObject | None = None,
    *,
    archive_verified: bool = False,
) -> GateResult:
    return evaluate_gate(
        candidate,
        "PUBLIC_ACTIVE",
        acceptance_contract,
        archive_verified=archive_verified,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Project SEAM Public Production fail-closed release gate"
    )
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--state", choices=PUBLIC_STATES, default="PUBLIC_ACTIVE")
    parser.add_argument("--acceptance-contract", type=Path)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        contract = load_json(args.acceptance_contract) if args.acceptance_contract else None
        result = evaluate_gate(
            load_json(args.candidate),
            args.state,
            contract,
        )
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        print(
            json.dumps(
                {"state": args.state, "passed": False, "errors": [str(exc)]},
                sort_keys=True,
            )
        )
        return 2
    payload = canonical_json(result.as_dict())
    print(payload)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(payload + "\n", encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
