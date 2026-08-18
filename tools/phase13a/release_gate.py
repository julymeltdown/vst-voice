#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

RUNTIME_RESULTS = {"NOT_RUN", "BLOCKED", "FAIL", "PASS"}
IMPLEMENTATION_STATES = {"NOT_STARTED", "SOURCE_READY", "CI_CONFIGURED", "TARGET_BUILD_PASS"}
GATES = {"G2", "G3", "G4", "G5"}
REQUIRED_EVIDENCE_FIELDS = {
    "osVersion",
    "hostVersion",
    "pluginFormat",
    "pluginSha256",
    "executedAt",
    "executor",
    "checks",
    "logs",
    "evidenceSha256",
}


@dataclass(frozen=True)
class GateResult:
    gate: str
    passed: bool
    blocked_ids: tuple[str, ...]
    errors: tuple[str, ...]

    def as_dict(self) -> dict[str, Any]:
        return {
            "gate": self.gate,
            "passed": self.passed,
            "blockedIds": list(self.blocked_ids),
            "errors": list(self.errors),
        }


def _validate_evidence(target_id: str, evidence: Any) -> list[str]:
    if not isinstance(evidence, list) or not evidence:
        return [f"{target_id}: PASS requires at least one evidence record"]
    errors: list[str] = []
    for index, record in enumerate(evidence):
        if not isinstance(record, dict):
            errors.append(f"{target_id}: evidence[{index}] must be an object")
            continue
        missing = sorted(REQUIRED_EVIDENCE_FIELDS - set(record))
        if missing:
            errors.append(
                f"{target_id}: evidence[{index}] missing required fields: {', '.join(missing)}"
            )
            continue
        logs = record["logs"]
        if not isinstance(logs, list) or not logs or not all(isinstance(path, str) and path for path in logs):
            errors.append(f"{target_id}: evidence[{index}].logs must contain non-empty paths")
        checks = record["checks"]
        if not isinstance(checks, dict) or not checks:
            errors.append(f"{target_id}: evidence[{index}].checks must contain executed checks")
        elif any(value != "PASS" for value in checks.values()):
            errors.append(f"{target_id}: evidence[{index}] requires every check to equal PASS")
        hashes = record["evidenceSha256"]
        if not isinstance(hashes, dict) or not hashes:
            errors.append(f"{target_id}: evidence[{index}].evidenceSha256 must be a non-empty object")
        elif isinstance(logs, list) and set(hashes) != set(logs):
            errors.append(f"{target_id}: evidence[{index}].evidenceSha256 keys must match logs")
        else:
            for path, digest in hashes.items():
                if not isinstance(path, str) or not isinstance(digest, str) or len(digest) != 64 or any(ch not in "0123456789abcdefABCDEF" for ch in digest):
                    errors.append(f"{target_id}: evidence[{index}] has invalid evidence SHA-256 for {path!r}")
        sha = record["pluginSha256"]
        if not isinstance(sha, str) or len(sha) != 64 or any(ch not in "0123456789abcdefABCDEF" for ch in sha):
            errors.append(f"{target_id}: evidence[{index}].pluginSha256 must be 64 hexadecimal characters")
    return errors


def evaluate_matrix(matrix: dict[str, Any], gate: str) -> GateResult:
    gate = gate.upper()
    errors: list[str] = []
    blocked: list[str] = []
    if gate not in GATES:
        return GateResult(gate, False, (), (f"Unsupported release gate: {gate}",))
    if matrix.get("schemaVersion") != 1:
        errors.append("validation matrix schemaVersion must equal 1")
    if matrix.get("policy") != "MANDATORY":
        errors.append("validation matrix policy must equal MANDATORY")
    targets = matrix.get("targets")
    if not isinstance(targets, list):
        return GateResult(gate, False, (), tuple(errors + ["targets must be an array"]))
    seen: set[str] = set()
    for raw in targets:
        if not isinstance(raw, dict):
            errors.append("each target must be an object")
            continue
        target_id = raw.get("id") or raw.get("name")
        if not isinstance(target_id, str) or not target_id:
            errors.append("each target requires a non-empty id")
            continue
        if target_id in seen:
            errors.append(f"duplicate target id: {target_id}")
            continue
        seen.add(target_id)
        implementation = raw.get("implementationState")
        runtime = raw.get("runtimeResult")
        if implementation not in IMPLEMENTATION_STATES:
            errors.append(f"{target_id}: invalid implementationState {implementation!r}")
        if runtime not in RUNTIME_RESULTS:
            errors.append(f"{target_id}: invalid runtimeResult {runtime!r}")
        mandatory_for = raw.get("mandatoryFor", [])
        if not isinstance(mandatory_for, list):
            errors.append(f"{target_id}: mandatoryFor must be an array")
            continue
        required = gate in {str(value).upper() for value in mandatory_for}
        if required and runtime != "PASS":
            blocked.append(target_id)
        if runtime == "PASS":
            errors.extend(_validate_evidence(target_id, raw.get("evidence")))
    passed = not errors and not blocked
    return GateResult(gate, passed, tuple(sorted(blocked)), tuple(errors))


def load_matrix(path: Path) -> dict[str, Any]:
    if path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("validation matrix exceeds 4 MiB")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("validation matrix root must be an object")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Project SEAM mandatory release-gate evaluator")
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("--matrix", type=Path, required=True)
    check.add_argument("--gate", choices=sorted(GATES), required=True)
    check.add_argument("--json-output", type=Path)
    check.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = evaluate_matrix(load_matrix(args.matrix), args.gate)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(json.dumps({"passed": False, "errors": [str(exc)]}, ensure_ascii=False))
        return 2
    payload = result.as_dict()
    text = json.dumps(payload, ensure_ascii=False, indent=2)
    print(text)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(text + "\n", encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed and not result.errors else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
