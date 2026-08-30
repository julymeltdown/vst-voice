#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime
from pathlib import Path, PurePosixPath
import re
from typing import Final


JsonValue = (
    str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
)
JsonObject = dict[str, JsonValue]

EXPECTED_IDS: Final[tuple[str, ...]] = tuple(
    f"PW-{index:03d}" for index in range(1, 21)
)
VALID_REQUIREMENT_STATUSES: Final[frozenset[str]] = frozenset(
    {"NOT_RUN", "BLOCKED", "FAIL", "PASS"}
)
VALID_GATE_STATUSES: Final[frozenset[str]] = frozenset({"BLOCKED", "PASSED"})
SHA256_RE: Final[re.Pattern[str]] = re.compile(r"^[0-9a-f]{64}$")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _safe_relative(value: str) -> bool:
    path = PurePosixPath(value)
    return (
        bool(value)
        and "\\" not in value
        and not path.is_absolute()
        and all(part not in {"", ".", ".."} for part in path.parts)
    )


def _valid_time(value: JsonValue) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def verify_contract(payload: JsonObject, root: Path) -> list[str]:
    root = root.resolve()
    errors: list[str] = []
    if payload.get("schemaVersion") != 1:
        errors.append("contract schemaVersion must be 1")
    gate = payload.get("gate")
    gate_status = gate.get("status") if isinstance(gate, dict) else None
    if gate_status not in VALID_GATE_STATUSES:
        errors.append("gate.status must be BLOCKED or PASSED")
    if payload.get("platform") != "windows":
        errors.append("contract platform must be windows")
    if payload.get("architecture") != "x86_64":
        errors.append("contract architecture must be x86_64")
    requirements = payload.get("requirements")
    if not isinstance(requirements, list):
        return errors + ["requirements must be an array"]
    ids = tuple(
        item.get("id") if isinstance(item, dict) else None for item in requirements
    )
    if ids != EXPECTED_IDS:
        errors.append(f"requirement IDs must be exactly {list(EXPECTED_IDS)}")
    mandatory_not_pass: list[str] = []
    has_pass_row = any(
        isinstance(item, dict) and item.get("status") == "PASS"
        for item in requirements
    )
    top_lineage = payload.get("candidateLineageId")
    top_artifact = payload.get("artifactRootSha256")
    top_installed = payload.get("installedTreeSha256")
    if gate_status == "PASSED" or has_pass_row:
        if not isinstance(top_lineage, str) or not top_lineage:
            errors.append("candidateLineageId is required when any PW row is PASS")
        for key, value in (
            ("artifactRootSha256", top_artifact),
            ("installedTreeSha256", top_installed),
        ):
            if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
                errors.append(f"{key} must be a lowercase SHA-256 when any PW row is PASS")
    for index, item in enumerate(requirements):
        if not isinstance(item, dict):
            errors.append(f"requirement at index {index} must be an object")
            continue
        requirement_id = item.get("id")
        label = requirement_id if isinstance(requirement_id, str) else f"index-{index}"
        if item.get("mandatory") is not True:
            errors.append(f"{label}: mandatory must be true")
        status = item.get("status")
        if status not in VALID_REQUIREMENT_STATUSES:
            errors.append(f"{label}: invalid status")
            continue
        if status != "PASS":
            mandatory_not_pass.append(label)
        evidence = item.get("evidence")
        if not isinstance(evidence, list):
            errors.append(f"{label}: evidence must be an array")
            continue
        if status == "PASS" and not evidence:
            errors.append(f"{label}: PASS requires Windows target evidence")
        for evidence_index, entry in enumerate(evidence):
            prefix = f"{label}: evidence[{evidence_index}]"
            if not isinstance(entry, dict):
                errors.append(f"{prefix} must be an object")
                continue
            if entry.get("requirementId") != requirement_id:
                errors.append(f"{prefix} cannot satisfy {label}")
            if entry.get("platform") != "windows":
                errors.append(f"{prefix} platform must be windows")
            if entry.get("architecture") != "x86_64":
                errors.append(f"{prefix} architecture must be x86_64")
            if entry.get("surface") != "standalone":
                errors.append(f"{prefix} surface must be standalone")
            if entry.get("status") != "PASS":
                errors.append(f"{prefix} status must be PASS")
            for key in ("recordId", "operatorId", "machineProfileId"):
                value = entry.get(key)
                if not isinstance(value, str) or not value:
                    errors.append(f"{prefix} {key} is required")
            if not _valid_time(entry.get("trustedTime")):
                errors.append(f"{prefix} trustedTime must be ISO-8601")
            lineage = entry.get("candidateLineageId")
            if not isinstance(lineage, str) or not lineage:
                errors.append(f"{prefix} candidateLineageId is required")
            elif lineage != top_lineage:
                errors.append(f"{prefix} candidate lineage differs from matrix")
            for key in ("artifactRootSha256", "installedTreeSha256"):
                digest = entry.get(key)
                if not isinstance(digest, str) or SHA256_RE.fullmatch(digest) is None:
                    errors.append(f"{prefix} {key} must be a lowercase SHA-256")
            if entry.get("artifactRootSha256") != top_artifact:
                errors.append(f"{prefix} artifact root differs from matrix")
            if entry.get("installedTreeSha256") != top_installed:
                errors.append(f"{prefix} installed tree differs from matrix")
            relative = entry.get("path")
            if not isinstance(relative, str) or not _safe_relative(relative):
                errors.append(f"{prefix} path must be safe and repository-relative")
                continue
            expected_sha256 = entry.get("sha256")
            if not isinstance(expected_sha256, str) or SHA256_RE.fullmatch(expected_sha256) is None:
                errors.append(f"{prefix} sha256 must be lowercase hexadecimal")
                continue
            candidate = root / relative
            if candidate.is_symlink():
                errors.append(f"{prefix} path cannot be symbolic")
                continue
            try:
                resolved = candidate.resolve(strict=True)
            except (FileNotFoundError, OSError):
                errors.append(f"{prefix} evidence file does not exist: {relative}")
                continue
            if resolved != root and root not in resolved.parents:
                errors.append(f"{prefix} path escapes the repository root")
            elif not resolved.is_file():
                errors.append(f"{prefix} path is not a file")
            elif _sha256(resolved) != expected_sha256:
                errors.append(f"{prefix} sha256 does not match: {relative}")
    if gate_status == "PASSED" and mandatory_not_pass:
        errors.append("gate cannot be PASSED while mandatory rows are not PASS")
    return errors


def verify_repository(root: Path) -> list[str]:
    resolved_root = root.resolve()
    path = resolved_root / "docs/product/public-windows-standalone-acceptance.json"
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return [f"invalid public Windows contract {path}: {exc}"]
    if not isinstance(payload, dict):
        return ["contract JSON root must be an object"]
    errors = verify_contract(payload, resolved_root)
    canonical = payload.get("canonicalDocument")
    if canonical != "docs/product/PUBLIC_WINDOWS_STANDALONE_ACCEPTANCE.md":
        errors.append("canonicalDocument path differs")
    elif not (resolved_root / canonical).is_file():
        errors.append("canonical public Windows document is missing")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify the Project SEAM public Windows standalone contract"
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    args = parser.parse_args()
    errors = verify_repository(args.root)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        print(f"PUBLIC_WINDOWS_STANDALONE_CONTRACT=FAIL errors={len(errors)}")
        return 1
    print("PUBLIC_WINDOWS_STANDALONE_CONTRACT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
