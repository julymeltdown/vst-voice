from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any


HEX40 = re.compile(r"^[0-9a-fA-F]{40}$")
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
STATE_FAMILIES = ("project", "media", "settings", "autosave", "catalog", "clap", "host")


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def _digest(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, str) or HEX64.fullmatch(value) is None:
        errors.append(f"{label} must be a 64-character hexadecimal digest")


def validate_predecessor(record: dict[str, Any], archive_root: Path | None = None) -> list[str]:
    errors: list[str] = []
    if record.get("schemaVersion") != 1:
        errors.append("schemaVersion must equal 1")
    if record.get("status") != "SIGNED_COHERENT":
        errors.append("predecessor status must be SIGNED_COHERENT")
    identity = record.get("releaseIdentity")
    if not isinstance(identity, dict) or not identity.get("version") or not identity.get("buildId"):
        errors.append("releaseIdentity version and buildId are required")
    if not isinstance(identity, dict) or not HEX40.fullmatch(str(identity.get("sourceCommit", ""))):
        errors.append("releaseIdentity.sourceCommit must be a 40-character commit")
    for key in ("bankSha256", "trustPolicySha256", "documentationSha256", "archiveSha256"):
        _digest(record.get(key), key, errors)
    packages = record.get("packages")
    if not isinstance(packages, dict):
        errors.append("packages must contain macos and windows records")
        packages = {}
    for platform in ("macos", "windows"):
        package = packages.get(platform)
        if not isinstance(package, dict):
            errors.append(f"packages.{platform} is required")
            continue
        if package.get("status") != "SIGNED":
            errors.append(f"packages.{platform}.status must be SIGNED")
        _digest(package.get("packageSha256"), f"packages.{platform}.packageSha256", errors)
        _digest(package.get("installedTreeSha256"), f"packages.{platform}.installedTreeSha256", errors)
        if platform == "macos" and package.get("notarizedStapled") is not True:
            errors.append("packages.macos.notarizedStapled must be true")
        if platform == "windows" and package.get("timestamped") is not True:
            errors.append("packages.windows.timestamped must be true")
    fixtures = record.get("stateFixtures")
    seen: set[str] = set()
    if not isinstance(fixtures, list):
        errors.append("stateFixtures must be a list")
        fixtures = []
    for index, fixture in enumerate(fixtures):
        label = f"stateFixtures[{index}]"
        if not isinstance(fixture, dict) or fixture.get("family") not in STATE_FAMILIES:
            errors.append(f"{label}.family is invalid")
            continue
        family = fixture["family"]
        if family in seen:
            errors.append(f"duplicate state fixture family: {family}")
        seen.add(family)
        for key in ("beforeSha256", "afterSha256", "archiveSha256"):
            _digest(fixture.get(key), f"{label}.{key}", errors)
        if archive_root is not None:
            relative = fixture.get("archivePath")
            if not isinstance(relative, str) or Path(relative).is_absolute() or ".." in Path(relative).parts:
                errors.append(f"{label}.archivePath is unsafe")
            elif not (archive_root / relative).is_file():
                errors.append(f"{label}.archivePath is missing from restored archive")
    if set(STATE_FAMILIES) != seen:
        errors.append("predecessor must retain every required persistent state family")
    return errors


def seal_predecessor(record: dict[str, Any], archive_root: Path | None = None) -> dict[str, Any]:
    errors = validate_predecessor(record, archive_root)
    if errors:
        raise ValueError("; ".join(errors))
    sealed = dict(record)
    sealed["recordSha256"] = sha256_json(record)
    return sealed


def write_predecessor(record: dict[str, Any], path: Path, archive_root: Path | None = None) -> None:
    sealed = seal_predecessor(record, archive_root)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(canonical_json(sealed) + "\n", encoding="utf-8")
