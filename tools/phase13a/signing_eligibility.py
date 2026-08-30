from __future__ import annotations

import json
from pathlib import Path

from tools.phase13a.payload_paths import PayloadAssemblyError, require_real_directory


def _object(path: Path) -> dict[str, object] | None:
    if path.is_symlink() or not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def production_source_trust_issues(source_root: Path) -> tuple[str, ...]:
    trust = _object(source_root.resolve() / "packaging/trust/release-trust-roots.json")
    if trust is None:
        return ("production trust-root declaration is missing or invalid",)
    if trust.get("testOnly") is not False:
        return ("production signing is blocked by the development trust root",)
    return ()


def production_payload_issues(payload_root: Path) -> tuple[str, ...]:
    try:
        payload = require_real_directory(payload_root, "payload root")
    except PayloadAssemblyError as error:
        return error.issues
    manifest = _object(payload / "release-payload-manifest.json")
    trust = _object(payload / "Trust/release-trust-roots.json")
    issues: list[str] = []
    if manifest is None:
        issues.append("release payload manifest is missing or invalid")
    else:
        if manifest.get("releaseEligible") is not True:
            issues.append("release payload is not production-eligible")
        if manifest.get("developmentTrustOnly") is not False:
            issues.append("release payload still declares development-only trust")
    if trust is None or trust.get("testOnly") is not False:
        issues.append("release payload does not contain a production trust root")
    return tuple(issues)
