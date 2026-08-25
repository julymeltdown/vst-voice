#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_entry(record: dict[str, Any], key: str, root: Path) -> list[str]:
    errors: list[str] = []
    entry = record.get(key)
    if not isinstance(entry, dict):
        return [f"record {key} entry is missing"]
    relative = entry.get("path")
    expected = entry.get("sha256")
    if not isinstance(relative, str) or not relative:
        return [f"record {key} path is missing"]
    if not isinstance(expected, str) or len(expected) != 64:
        errors.append(f"record {key} sha256 is missing or malformed")
    path = (root / relative).resolve()
    candidate = root / relative
    if candidate.is_symlink():
        return errors + [f"record {key} artifact is a symbolic link: {relative}"]
    try:
        path.relative_to(root.resolve())
    except ValueError:
        return errors + [f"record {key} path escapes packet root: {relative}"]
    if not path.is_file() or path.stat().st_size == 0:
        return errors + [f"record {key} artifact is missing or empty: {relative}"]
    if isinstance(expected, str) and len(expected) == 64:
        actual = sha256(path)
        if actual != expected:
            errors.append(
                f"record {key} digest mismatch: expected {expected}, got {actual}"
            )
    return errors


def verify_runner_metadata(record: dict[str, Any], root: Path) -> list[str]:
    entry = record.get("runnerMetadata")
    if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
        return ["runner metadata entry is missing"]
    candidate = root / entry["path"]
    if candidate.is_symlink():
        return ["runner metadata is a symbolic link"]
    path = candidate.resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError:
        return ["runner metadata escapes packet root"]
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return [f"runner metadata cannot be read: {error}"]
    if not isinstance(value, dict) or any(
        not isinstance(value.get(field), str) or not value[field]
        for field in ("runnerOs", "runnerArchitecture")
    ):
        return ["runner metadata must contain non-empty runnerOs and runnerArchitecture"]
    return []


def verify_packet(record_path: Path, root: Path) -> list[str]:
    try:
        record = json.loads(record_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [f"unable to read record: {error}"]
    if not isinstance(record, dict):
        return ["record is not a JSON object"]
    errors: list[str] = []
    if record.get("schemaVersion") != 1:
        errors.append("record schemaVersion must be 1")
    if record.get("recordType") != "phase12c-target-runtime":
        errors.append("record recordType is invalid")
    if record.get("platform") not in {"macos", "windows"}:
        errors.append("record platform must be macos or windows")
    if record.get("implementationState") != "TARGET_BUILD_PASS":
        errors.append("record implementationState must be TARGET_BUILD_PASS")
    if record.get("runtimeResult") != "PASS":
        errors.append("record runtimeResult must be PASS")
    for key in ("summary", "screenshot", "audio", "runnerMetadata", "hostLog"):
        errors.extend(verify_entry(record, key, root))
    errors.extend(verify_runner_metadata(record, root))
    derived = record.get("derivedPng")
    if derived is not None:
        errors.extend(verify_entry(record, "derivedPng", root))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    errors = verify_packet(args.record.resolve(), args.root.resolve())
    if errors:
        for error in errors:
            print(f"[phase12c-target-runtime-packet] ERROR: {error}")
        return 1
    print("[phase12c-target-runtime-packet] PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
