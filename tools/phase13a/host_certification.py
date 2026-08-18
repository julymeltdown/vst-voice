#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
from typing import Any

RUNTIME_RESULTS = {"NOT_RUN", "BLOCKED", "FAIL", "PASS"}
CHECK_NAMES = {
    "scan", "installDiscovery", "guiLifecycle", "stateRestore", "transport",
    "tempoAutomation", "offlineExport", "unloadReload", "channelMatrix",
    "sampleRateMatrix", "bufferMatrix", "projectReopen",
}
REQUIRED_PASS_FIELDS = {
    "targetId", "runtimeResult", "osVersion", "hostVersion", "pluginFormat",
    "pluginSha256", "executedAt", "executor", "checks", "evidence",
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_record(record: dict[str, Any], evidence_root: Path) -> list[str]:
    errors: list[str] = []
    result = record.get("runtimeResult")
    if result not in RUNTIME_RESULTS:
        return [f"runtimeResult must be one of {sorted(RUNTIME_RESULTS)}"]
    target_id = record.get("targetId")
    if not isinstance(target_id, str) or not target_id:
        errors.append("targetId must be a non-empty string")
    checks = record.get("checks", {})
    evidence = record.get("evidence", [])
    if result != "PASS":
        if checks:
            errors.append("checks must be empty unless runtimeResult is PASS")
        if evidence:
            errors.append("evidence must be empty unless runtimeResult is PASS")
        return errors
    missing = sorted(REQUIRED_PASS_FIELDS - set(record))
    if missing:
        return errors + [f"PASS record missing fields: {', '.join(missing)}"]
    sha = record.get("pluginSha256")
    if not isinstance(sha, str) or len(sha) != 64 or any(ch not in "0123456789abcdefABCDEF" for ch in sha):
        errors.append("pluginSha256 must be 64 hexadecimal characters")
    if not isinstance(checks, dict):
        errors.append("checks must be an object")
    else:
        missing_checks = sorted(CHECK_NAMES - set(checks))
        if missing_checks:
            errors.append(f"PASS record missing checks: {', '.join(missing_checks)}")
        for name, value in checks.items():
            if name not in CHECK_NAMES:
                errors.append(f"unknown check: {name}")
            if value != "PASS":
                errors.append(f"PASS record requires {name}=PASS")
    evidence_hashes: dict[str, str] = {}
    if not isinstance(evidence, list) or not evidence:
        errors.append("PASS record requires evidence paths")
    else:
        root = evidence_root.resolve()
        for relative in evidence:
            if not isinstance(relative, str) or not relative:
                errors.append("evidence paths must be non-empty strings")
                continue
            candidate = (root / relative).resolve()
            try:
                candidate.relative_to(root)
            except ValueError:
                errors.append(f"evidence path escapes evidence root: {relative}")
                continue
            if not candidate.is_file() or candidate.stat().st_size == 0:
                errors.append(f"evidence file does not exist or is empty: {relative}")
                continue
            evidence_hashes[relative] = _sha256(candidate)
    for name in ("osVersion", "hostVersion", "pluginFormat", "executedAt", "executor"):
        if not isinstance(record.get(name), str) or not record[name].strip():
            errors.append(f"{name} must be a non-empty string")
    declared_hashes = record.get("evidenceSha256")
    if declared_hashes is not None and declared_hashes != evidence_hashes:
        errors.append("evidenceSha256 does not match the actual evidence files")
    if not errors:
        record["evidenceSha256"] = evidence_hashes
    return errors


def apply_record(matrix: dict[str, Any], record: dict[str, Any]) -> dict[str, Any]:
    updated = copy.deepcopy(matrix)
    target_id = record["targetId"]
    matches = [target for target in updated.get("targets", []) if target.get("id") == target_id]
    if len(matches) != 1:
        raise ValueError(f"targetId must match exactly one validation target: {target_id}")
    target = matches[0]
    target["runtimeResult"] = record["runtimeResult"]
    if record["runtimeResult"] == "PASS":
        target["implementationState"] = "TARGET_BUILD_PASS"
        target["evidence"] = [{
            "osVersion": record["osVersion"],
            "hostVersion": record["hostVersion"],
            "pluginFormat": record["pluginFormat"],
            "pluginSha256": record["pluginSha256"],
            "executedAt": record["executedAt"],
            "executor": record["executor"],
            "checks": copy.deepcopy(record["checks"]),
            "logs": copy.deepcopy(record["evidence"]),
            "evidenceSha256": copy.deepcopy(record["evidenceSha256"]),
        }]
    else:
        target["evidence"] = []
    return updated


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Record actual target OS and commercial DAW certification evidence")
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        matrix = json.loads(args.matrix.read_text(encoding="utf-8"))
        record = json.loads(args.record.read_text(encoding="utf-8"))
        if not isinstance(matrix, dict) or not isinstance(record, dict):
            raise ValueError("matrix and record roots must be objects")
        errors = validate_record(record, args.evidence_root)
        if errors:
            for error in errors:
                print(f"ERROR: {error}")
            return 3
        updated = apply_record(matrix, record)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(updated, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}")
        return 2
    print(f"PHASE13A_CERTIFICATION_RECORDED={record['targetId']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
