from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from common import safe_relative_path, sha256_file


def audit_character(character_root: Path, profile: dict[str, Any]) -> dict[str, Any]:
    character_root = Path(character_root)
    errors: list[str] = []
    hashes: dict[str, str] = {}
    for relative in profile.get("requiredPaths", []):
        if not safe_relative_path(relative):
            errors.append(f"unsafe required character path: {relative}")
            continue
        path = character_root / relative
        try:
            if path.is_symlink():
                errors.append(f"character asset must not be a symlink: {relative}")
                continue
            resolved = path.resolve(strict=True)
            root = character_root.resolve(strict=True)
            if root not in resolved.parents:
                errors.append(f"character asset escapes root: {relative}")
                continue
            if not resolved.is_file():
                errors.append(f"character asset is not regular: {relative}")
                continue
            hashes[relative] = sha256_file(resolved)
        except OSError:
            errors.append(f"character asset is missing: {relative}")
    manifest_path = character_root / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:
        errors.append(f"character manifest cannot be loaded: {exc}")
        manifest = {}
    states = manifest.get("states", {}) if isinstance(manifest, dict) else {}
    for state in profile.get("requiredStates", []):
        relative = states.get(state)
        if not isinstance(relative, str):
            errors.append(f"character runtime state is missing: {state}")
        elif not (character_root / relative).is_file():
            errors.append(f"character runtime state file is missing: {state} -> {relative}")
    lod_directory = character_root / "lod"
    lod_count = len([path for path in lod_directory.glob("*.obj") if path.is_file() and not path.is_symlink()]) if lod_directory.exists() else 0
    minimum_lods = int(profile.get("minimumLods", 0))
    if lod_count < minimum_lods:
        errors.append(f"minimumLods requires {minimum_lods}, found {lod_count}")
    return {
        "passed": not errors,
        "errors": errors,
        "characterId": manifest.get("characterId", "") if isinstance(manifest, dict) else "",
        "version": manifest.get("version", "") if isinstance(manifest, dict) else "",
        "lodCount": lod_count,
        "assetSha256": dict(sorted(hashes.items())),
    }
