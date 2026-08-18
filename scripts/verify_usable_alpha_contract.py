#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any

CONTRACT_LINK = "docs/product/USABLE_ALPHA_ACCEPTANCE.md"
EXPECTED_IDS = [f"UA-{index:03d}" for index in range(1, 21)]
VALID_REQUIREMENT_STATUSES = {"NOT_RUN", "BLOCKED", "FAIL", "PASS"}
VALID_GATE_STATUSES = {"BLOCKED", "PASSED"}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _load_json(path: Path, errors: list[str]) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        errors.append(f"missing contract JSON: {path}")
        return None
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        errors.append(f"invalid contract JSON {path}: {exc}")
        return None
    if not isinstance(value, dict):
        errors.append("contract JSON root must be an object")
        return None
    return value


def verify_repository(root: Path) -> list[str]:
    root = root.resolve()
    errors: list[str] = []

    readme = root / "README.md"
    try:
        readme_text = readme.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        errors.append(f"cannot read README: {exc}")
        readme_text = ""
    if CONTRACT_LINK not in readme_text:
        errors.append(f"README must link the canonical contract: {CONTRACT_LINK}")

    canonical_doc = root / CONTRACT_LINK
    if not canonical_doc.is_file():
        errors.append(f"missing canonical English contract: {canonical_doc}")

    payload = _load_json(root / "docs/product/usable-alpha-acceptance.json", errors)
    if payload is None:
        return errors

    if payload.get("schemaVersion") != 1:
        errors.append("contract schemaVersion must be 1")

    gate = payload.get("gate")
    if not isinstance(gate, dict):
        errors.append("gate must be an object")
        gate = {}
    gate_status = gate.get("status")
    if gate_status not in VALID_GATE_STATUSES:
        errors.append(f"gate.status must be one of {sorted(VALID_GATE_STATUSES)}")

    requirements = payload.get("requirements")
    if not isinstance(requirements, list):
        errors.append("requirements must be an array")
        return errors

    ids = [item.get("id") if isinstance(item, dict) else None for item in requirements]
    if ids != EXPECTED_IDS:
        errors.append(f"requirement IDs must be exactly {EXPECTED_IDS}")

    mandatory_not_pass: list[str] = []
    for index, item in enumerate(requirements):
        if not isinstance(item, dict):
            errors.append(f"requirement at index {index} must be an object")
            continue
        requirement_id = str(item.get("id", f"index-{index}"))
        mandatory = item.get("mandatory")
        if mandatory is not True:
            errors.append(f"{requirement_id}: mandatory must be true")
        status = item.get("status")
        if status not in VALID_REQUIREMENT_STATUSES:
            errors.append(
                f"{requirement_id}: status must be one of {sorted(VALID_REQUIREMENT_STATUSES)}"
            )
            continue
        if mandatory is True and status != "PASS":
            mandatory_not_pass.append(requirement_id)

        evidence = item.get("evidence")
        if not isinstance(evidence, list):
            errors.append(f"{requirement_id}: evidence must be an array")
            continue
        if status == "PASS" and not evidence:
            errors.append(f"{requirement_id}: PASS requires evidence path and sha256")
        for evidence_index, entry in enumerate(evidence):
            prefix = f"{requirement_id}: evidence[{evidence_index}]"
            if not isinstance(entry, dict):
                errors.append(f"{prefix} must be an object")
                continue
            relative_path = entry.get("path")
            expected_hash = entry.get("sha256")
            if not isinstance(relative_path, str) or not relative_path:
                errors.append(f"{prefix} path is required")
                continue
            evidence_path = Path(relative_path)
            if evidence_path.is_absolute() or ".." in evidence_path.parts:
                errors.append(f"{prefix} path must be a safe repository-relative path")
                continue
            if not isinstance(expected_hash, str) or not SHA256_RE.fullmatch(expected_hash):
                errors.append(f"{prefix} sha256 must be 64 lowercase hexadecimal characters")
                continue
            resolved = (root / evidence_path).resolve()
            try:
                resolved.relative_to(root)
            except ValueError:
                errors.append(f"{prefix} escapes the repository root")
                continue
            if not resolved.is_file():
                errors.append(f"{prefix} file does not exist: {relative_path}")
                continue
            if _sha256(resolved) != expected_hash:
                errors.append(f"{prefix} sha256 does not match: {relative_path}")

    if gate_status == "PASSED" and mandatory_not_pass:
        errors.append(
            "gate cannot be PASSED while mandatory requirements are not PASS: "
            + ", ".join(mandatory_not_pass)
        )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify the Project SEAM Usable Alpha gate")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors = verify_repository(args.root)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        print(f"USABLE_ALPHA_CONTRACT=FAIL errors={len(errors)}")
        return 1
    print("USABLE_ALPHA_CONTRACT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
