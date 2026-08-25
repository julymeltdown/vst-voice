from __future__ import annotations

import hashlib
from pathlib import Path, PurePosixPath

from tools.engineering_remediation_contract import JsonValue, SHA256


def _locator(value: JsonValue, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or not value or "\\" in value:
        errors.append(f"{label} must be a nonempty portable relative path")
        return None
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        errors.append(f"{label} must stay inside the evidence archive")
        return None
    return value


def _digest(value: JsonValue, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or SHA256.fullmatch(value) is None:
        errors.append(f"{label} must be a full lowercase hexadecimal digest")
        return None
    if set(value) == {"0"}:
        errors.append(f"{label} must not be a placeholder digest")
        return None
    return value


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _contains_symlink(root: Path, locator: str) -> bool:
    if root.is_symlink():
        return True
    current = root
    for component in PurePosixPath(locator).parts:
        current /= component
        if current.is_symlink():
            return True
    return False


def validate_artifact(
    value: JsonValue, label: str, root: Path | None, errors: list[str]
) -> None:
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        errors.append(f"{label} must be an object")
        return
    locator = _locator(value.get("locator"), f"{label}.locator", errors)
    digest = _digest(value.get("sha256"), f"{label}.sha256", errors)
    if root is None or locator is None or digest is None:
        return
    if _contains_symlink(root, locator):
        errors.append(f"{label} path contains a symlink")
        return
    evidence_root = root.resolve()
    path = evidence_root / locator
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(evidence_root)
    except (OSError, ValueError):
        errors.append(f"{label} is missing or outside the evidence root")
        return
    if not resolved.is_file():
        errors.append(f"{label} must reference a regular file")
        return
    if _sha256_file(resolved) != digest:
        errors.append(f"{label} digest does not match the referenced bytes")
