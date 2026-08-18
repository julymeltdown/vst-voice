from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any

from common import safe_relative_path


def audit_voicebank(bank_root: Path, profile: dict[str, Any]) -> dict[str, Any]:
    errors: list[str] = []
    bank_root = Path(bank_root)
    manifest_path = bank_root / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return {"passed": False, "errors": [f"manifest cannot be loaded: {exc}"], "enabledUnits": 0, "pitchLayers": 0}
    units = manifest.get("units")
    if not isinstance(units, list):
        return {"passed": False, "errors": ["manifest units must be an array"], "enabledUnits": 0, "pitchLayers": 0}
    enabled = [unit for unit in units if isinstance(unit, dict) and unit.get("enabled", True)]
    roots = {unit.get("rootMidi") for unit in enabled if isinstance(unit.get("rootMidi"), int)}
    kinds = Counter(str(unit.get("kind", "")) for unit in enabled)
    styles = {str(unit.get("style", "")) for unit in enabled}
    phones = {str(phone) for unit in enabled for phone in unit.get("phones", []) if isinstance(phone, str)}
    for unit in enabled:
        audio = unit.get("audio")
        if not safe_relative_path(audio):
            errors.append(f"unit {unit.get('id', '<unknown>')} has an unsafe audio path")
            continue
        path = bank_root / audio
        try:
            if path.is_symlink() or not path.resolve(strict=True).is_file():
                errors.append(f"unit audio is missing or not a regular file: {audio}")
            elif bank_root.resolve(strict=True) not in path.resolve(strict=True).parents:
                errors.append(f"unit audio escapes bank root: {audio}")
        except OSError:
            errors.append(f"unit audio is missing: {audio}")
    minimum_units = int(profile.get("minimumEnabledUnits", 0))
    if len(enabled) < minimum_units:
        errors.append(f"minimumEnabledUnits requires {minimum_units}, found {len(enabled)}")
    minimum_layers = int(profile.get("minimumPitchLayers", 0))
    if len(roots) < minimum_layers:
        errors.append(f"minimumPitchLayers requires {minimum_layers}, found {len(roots)}")
    for kind, minimum in profile.get("requiredKinds", {}).items():
        if kinds[kind] < int(minimum):
            errors.append(f"required kind {kind} requires {minimum}, found {kinds[kind]}")
    for style in profile.get("requiredStyles", []):
        if style not in styles:
            errors.append(f"required style is missing: {style}")
    for phone in profile.get("requiredPhones", []):
        if phone not in phones:
            errors.append(f"required phone is missing: {phone}")
    return {
        "passed": not errors,
        "errors": errors,
        "voicebankId": manifest.get("id", ""),
        "version": manifest.get("version", ""),
        "enabledUnits": len(enabled),
        "pitchLayers": len(roots),
        "kinds": dict(sorted(kinds.items())),
        "styles": sorted(styles),
        "phones": sorted(phones),
    }
