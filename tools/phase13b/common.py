from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

ALLOWED_RESULTS = {"NOT_RUN", "BLOCKED", "FAIL", "PASS"}
MAX_EVIDENCE_BYTES = 64 * 1024 * 1024


@dataclass(frozen=True)
class GateResult:
    passed: bool
    errors: list[str] = field(default_factory=list)
    blocked_targets: list[str] = field(default_factory=list)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path, maximum_bytes: int = 8 * 1024 * 1024) -> dict[str, Any]:
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"JSON input must be a regular file: {path}")
    size = path.stat().st_size
    if size <= 0:
        raise ValueError(f"JSON input must not be empty: {path}")
    if size > maximum_bytes:
        raise ValueError(f"JSON input exceeds maximum size of {maximum_bytes} bytes: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"JSON input cannot be loaded: {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def safe_relative_path(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    candidate = PurePosixPath(value)
    if candidate.is_absolute():
        return False
    return all(part not in {"", ".", ".."} for part in candidate.parts)


def validate_evidence(record: Any, root: Path, maximum_bytes: int = MAX_EVIDENCE_BYTES) -> list[str]:
    errors: list[str] = []
    if not isinstance(record, dict):
        return ["evidence record must be an object"]
    path_text = record.get("path")
    if not safe_relative_path(path_text):
        return ["evidence path must be a safe relative POSIX path"]
    for key in ("sha256", "executedAt", "reviewer"):
        if not isinstance(record.get(key), str) or not record[key].strip():
            errors.append(f"evidence {key} is required")
    if not (record.get("kind") or record.get("type")):
        errors.append("evidence kind/type is required")
    if "result" in record and record.get("result") != "PASS":
        errors.append("evidence result must be PASS when attached to a PASS gate")
    digest = record.get("sha256", "")
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdefABCDEF" for c in digest):
        errors.append("evidence sha256 must be a 64-character hexadecimal digest")
    path = root / path_text
    try:
        if path.is_symlink():
            errors.append("evidence file must not be a symbolic link")
            return errors
        resolved_root = root.resolve(strict=True)
        resolved = path.resolve(strict=True)
        if resolved_root != resolved and resolved_root not in resolved.parents:
            errors.append("evidence path escapes the evidence root")
            return errors
        if not resolved.is_file():
            errors.append(f"evidence file does not exist or is not regular: {path_text}")
            return errors
        size = resolved.stat().st_size
        if size <= 0:
            errors.append(f"evidence file must not be empty: {path_text}")
        if size > maximum_bytes:
            errors.append(f"evidence file exceeds maximum size of {maximum_bytes} bytes: {path_text}")
        if isinstance(digest, str) and len(digest) == 64 and sha256_file(resolved) != digest.lower():
            errors.append(f"evidence sha256 mismatch: {path_text}")
    except FileNotFoundError:
        errors.append(f"evidence file does not exist: {path_text}")
    except OSError as exc:
        errors.append(f"evidence file cannot be inspected: {path_text}: {exc}")
    return errors


def validate_requirement_map(requirements: Any, categories: Iterable[str], root: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    blocked: list[str] = []
    if not isinstance(requirements, dict):
        return ["requirements must be an object"], list(categories)
    for category in categories:
        item = requirements.get(category)
        if not isinstance(item, dict):
            errors.append(f"missing requirement: {category}")
            continue
        result = item.get("result", item.get("status"))
        if result not in ALLOWED_RESULTS:
            errors.append(f"requirement {category} has invalid result")
            continue
        if result in {"NOT_RUN", "BLOCKED"}:
            blocked.append(category)
            if item.get("evidence"):
                errors.append(f"requirement {category} must not attach evidence while {result}")
            continue
        if result == "FAIL":
            errors.append(f"requirement {category} failed")
            continue
        records = item.get("evidence")
        if not isinstance(records, list) or not records:
            errors.append(f"requirement {category} PASS requires evidence")
            continue
        for record in records:
            errors.extend(f"{category}: {message}" for message in validate_evidence(record, root))
    return errors, blocked


def resolve_component_root(root: Path, relative: Any, label: str) -> tuple[Path | None, list[str]]:
    if not safe_relative_path(relative):
        return None, [f"{label} must be a safe relative path"]
    root = Path(root)
    candidate = root / relative
    try:
        resolved_root = root.resolve(strict=True)
        if candidate.is_symlink():
            return None, [f"{label} must not be a symbolic link"]
        resolved = candidate.resolve(strict=True)
        if resolved_root != resolved and resolved_root not in resolved.parents:
            return None, [f"{label} escapes the dossier root"]
        if not resolved.is_dir():
            return None, [f"{label} must reference a directory"]
        return resolved, []
    except FileNotFoundError:
        return None, [f"{label} does not exist"]
    except OSError as exc:
        return None, [f"{label} cannot be inspected: {exc}"]
