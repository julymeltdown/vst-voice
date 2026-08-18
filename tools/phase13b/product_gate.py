from __future__ import annotations

from pathlib import Path
from typing import Any

from common import (
    MAX_EVIDENCE_BYTES,
    GateResult,
    safe_relative_path,
    sha256_file,
    validate_evidence,
)

_PHASE13A_REQUIRED_FIELDS = {
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


def _is_hex_digest(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdefABCDEF" for character in value)
    )


def _validate_phase13a_evidence(record: dict[str, Any], root: Path) -> list[str]:
    errors: list[str] = []
    missing = sorted(_PHASE13A_REQUIRED_FIELDS - set(record))
    if missing:
        return [f"evidence record missing required fields: {', '.join(missing)}"]

    for field in ("osVersion", "hostVersion", "pluginFormat", "executedAt", "executor"):
        if not isinstance(record.get(field), str) or not record[field].strip():
            errors.append(f"evidence {field} is required")

    if not _is_hex_digest(record.get("pluginSha256")):
        errors.append("evidence pluginSha256 must be a 64-character hexadecimal digest")

    checks = record.get("checks")
    if not isinstance(checks, dict) or not checks:
        errors.append("evidence checks must be a non-empty object")
    elif any(value != "PASS" for value in checks.values()):
        errors.append("evidence checks must all equal PASS")

    logs = record.get("logs")
    hashes = record.get("evidenceSha256")
    if not isinstance(logs, list) or not logs:
        errors.append("evidence logs must be a non-empty array")
        return errors
    if not isinstance(hashes, dict) or not hashes:
        errors.append("evidence evidenceSha256 must be a non-empty object")
        return errors
    if set(logs) != set(hashes):
        errors.append("evidence evidenceSha256 keys must match logs")
        return errors

    try:
        resolved_root = root.resolve(strict=True)
    except OSError as exc:
        return errors + [f"evidence root cannot be inspected: {exc}"]

    for relative in logs:
        if not safe_relative_path(relative):
            errors.append("evidence log path must be a safe relative POSIX path")
            continue
        digest = hashes.get(relative)
        if not _is_hex_digest(digest):
            errors.append(f"evidence sha256 must be valid for log: {relative}")
            continue
        path = root / relative
        try:
            if path.is_symlink():
                errors.append(f"evidence log must not be a symbolic link: {relative}")
                continue
            resolved = path.resolve(strict=True)
            if resolved_root != resolved and resolved_root not in resolved.parents:
                errors.append(f"evidence log path escapes evidence root: {relative}")
                continue
            if not resolved.is_file():
                errors.append(f"evidence log is not a regular file: {relative}")
                continue
            size = resolved.stat().st_size
            if size == 0:
                errors.append(f"evidence log must not be empty: {relative}")
                continue
            if size > MAX_EVIDENCE_BYTES:
                errors.append(
                    f"evidence log exceeds maximum size of {MAX_EVIDENCE_BYTES} bytes: {relative}"
                )
                continue
            if sha256_file(resolved) != digest.lower():
                errors.append(f"evidence sha256 mismatch: {relative}")
        except FileNotFoundError:
            errors.append(f"evidence log does not exist: {relative}")
        except OSError as exc:
            errors.append(f"evidence log cannot be inspected: {relative}: {exc}")
    return errors


def _validate_external_evidence(record: Any, root: Path) -> list[str]:
    if not isinstance(record, dict):
        return ["evidence record must be an object"]
    if "path" in record:
        return validate_evidence(record, root)
    return _validate_phase13a_evidence(record, root)


def evaluate_product(
    voicebank: GateResult,
    character: GateResult,
    matrix: dict,
    root: Path | None = None,
) -> dict:
    errors = [f"voicebank: {error}" for error in voicebank.errors]
    errors.extend(f"character: {error}" for error in character.errors)
    blocked = list(voicebank.blocked_targets) + list(character.blocked_targets)
    for target in matrix.get("targets", []):
        if target.get("mandatory") is not True:
            continue
        target_id = str(target.get("id", target.get("name", "<unknown>")))
        if target.get("runtimeResult") != "PASS":
            blocked.append(target_id)
            continue
        evidence = target.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            errors.append(f"mandatory target {target_id} PASS requires evidence")
            blocked.append(target_id)
            continue
        if root is None:
            errors.append(f"mandatory target {target_id} PASS requires an evidence root")
            blocked.append(target_id)
            continue
        target_errors: list[str] = []
        for record in evidence:
            target_errors.extend(_validate_external_evidence(record, Path(root)))
        if target_errors:
            errors.extend(f"mandatory target {target_id}: {message}" for message in target_errors)
            blocked.append(target_id)
    return {
        "passed": voicebank.passed and character.passed and not errors and not blocked,
        "errors": errors,
        "blockedTargets": sorted(set(blocked)),
        "unresolvedMandatoryCount": len(set(blocked)) + len(errors),
    }
