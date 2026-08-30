from __future__ import annotations

import json
import plistlib
from pathlib import Path


def _json(path: Path) -> dict[str, object] | None:
    if path.is_symlink() or not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def _identity_issue(
    path: Path,
    expected: dict[str, str | int],
    label: str,
) -> str | None:
    return None if _json(path) == expected else f"{label} identity differs"


def validate_surface_identities(
    payload: Path,
    platform: str,
    expected: dict[str, str | int],
) -> tuple[str, ...]:
    issues: list[str] = []
    if platform == "macos-arm64":
        sidecars = (
            (
                "standalone",
                payload
                / "Standalone/Project SEAM.app/Contents/Resources/RELEASE_IDENTITY.json",
            ),
            (
                "CLAP",
                payload
                / "CLAP/ProjectSEAMEditor.clap/Contents/Resources/RELEASE_IDENTITY.json",
            ),
        )
        wrappers = (
            (
                "VST3",
                payload
                / "VST3/ProjectSEAMEditor.vst3/Contents/Resources/wrapper-manifest.json",
            ),
            (
                "AUv2",
                payload
                / "AU/ProjectSEAMEditor.component/Contents/Resources/wrapper-manifest.json",
            ),
        )
        info_path = payload / "Standalone/Project SEAM.app/Contents/Info.plist"
        try:
            info = plistlib.loads(info_path.read_bytes())
        except (OSError, plistlib.InvalidFileException, ValueError):
            info = {}
        if (
            not isinstance(info, dict)
            or info.get("CFBundleShortVersionString") != expected["version"]
            or info.get("ProjectSEAMBuildID") != expected["buildId"]
            or info.get("ProjectSEAMSourceCommit") != expected["sourceCommit"]
        ):
            issues.append("standalone Info.plist identity differs")
    else:
        sidecars = (
            (
                "standalone",
                payload / "Standalone/Resources/RELEASE_IDENTITY.json",
            ),
            (
                "CLAP",
                payload / "CLAP/ProjectSEAMEditor.resources/RELEASE_IDENTITY.json",
            ),
        )
        wrappers = (
            (
                "VST3",
                payload / "VST3/ProjectSEAMEditor.vst3/wrapper-manifest.json",
            ),
        )
    for label, path in sidecars:
        if issue := _identity_issue(path, expected, label):
            issues.append(issue)
    for label, path in wrappers:
        value = _json(path)
        if (
            value is None
            or value.get("version") != expected["version"]
            or value.get("releaseIdentity") != expected
        ):
            issues.append(f"{label} wrapper identity differs")
    return tuple(issues)
