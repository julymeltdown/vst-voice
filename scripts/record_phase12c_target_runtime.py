#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


REQUIRED_BOOLEAN_FIELDS = (
    "guiCreated",
    "guiVisible",
    "screenshotWritten",
    "audioWritten",
    "offlineRenderAccepted",
    "activeLoadRejected",
    "inactiveGuiLoadAccepted",
    "stateRoundTrip",
)


class EvidenceError(ValueError):
    pass


def validate_runner_metadata(path: Path) -> None:
    if path.is_symlink() or not path.is_file():
        raise EvidenceError(f"runner metadata must be a regular file: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise EvidenceError(f"runner metadata cannot be read: {path}") from error
    if not isinstance(value, dict) or any(
        not isinstance(value.get(field), str) or not value[field]
        for field in ("runnerOs", "runnerArchitecture")
    ):
        raise EvidenceError(
            "runner metadata must contain non-empty runnerOs and runnerArchitecture"
        )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def evidence(path: Path, root: Path) -> dict[str, str]:
    if path.is_symlink():
        raise EvidenceError(f"evidence artifact cannot be a symbolic link: {path}")
    resolved_root = root.resolve()
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(resolved_root)
    except ValueError as error:
        raise EvidenceError(f"evidence artifact escapes packet root: {path}") from error
    if not resolved.is_file() or resolved.stat().st_size == 0:
        raise EvidenceError(f"evidence artifact is missing or empty: {path}")
    return {"path": relative.as_posix(), "sha256": sha256(resolved)}


def validate_summary(summary: dict[str, Any], root: Path, platform: str) -> list[str]:
    errors: list[str] = []
    expected_api = {"macos": "cocoa", "windows": "win32"}.get(platform)
    if summary.get("pluginId") != "com.project-seam.editor":
        errors.append("summary pluginId is not the canonical editor")
    if expected_api is None:
        errors.append("platform must be windows or macos")
    elif summary.get("hostApi") != expected_api:
        errors.append(
            f"summary hostApi {summary.get('hostApi')!r} does not match {platform}"
        )
    for field in ("fixtureId", "fixtureVersion", "fixtureContentHash"):
        if not isinstance(summary.get(field), str) or not summary[field]:
            errors.append(f"summary {field} is missing")
    if not isinstance(summary.get("fixtureContentHash"), str) or len(summary.get("fixtureContentHash", "")) != 64:
        errors.append("summary fixtureContentHash is not a SHA-256 digest")
    if summary.get("result") != "PASS":
        errors.append("summary result is not PASS")
    for field in REQUIRED_BOOLEAN_FIELDS:
        if summary.get(field) is not True:
            errors.append(f"summary {field} is not true")
    if not isinstance(summary.get("noteInputEnergy"), (int, float)) or summary["noteInputEnergy"] <= 0.01:
        errors.append("summary noteInputEnergy is not positive")
    if summary.get("capturedFrames", 0) <= 0:
        errors.append("summary capturedFrames is not positive")
    if summary.get("outputChannels") != 4:
        errors.append("summary outputChannels is not 4")
    if summary.get("restartRequests") != 1:
        errors.append("summary restartRequests is not exactly 1")
    if summary.get("processRequests", 0) <= 0:
        errors.append("summary processRequests is not positive")
    if summary.get("stateRoundTrip") is not True or summary.get("stateBytesEqual") is not True:
        errors.append("summary state round trip is not byte-identical")
    if summary.get("stateBytes", 0) <= 0 or summary.get("restoredStateBytes", 0) <= 0:
        errors.append("summary state byte counts are not positive")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--derived-png", type=Path)
    parser.add_argument("--host-log", type=Path)
    parser.add_argument("--runner-metadata", type=Path, required=True)
    args = parser.parse_args()

    root = args.build_root.resolve()
    summary_path = root / "phase11-clap-editor-summary.json"
    screenshot_path = root / "phase11-clap-editor.ppm"
    audio_path = root / "phase11-clap-editor-live.wav"
    errors: list[str] = []
    try:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        summary = {}
        errors.append(f"unable to read summary: {error}")
    if isinstance(summary, dict):
        errors.extend(validate_summary(summary, root, args.platform))
    else:
        errors.append("summary is not a JSON object")
    for path in (screenshot_path, audio_path):
        if not path.is_file() or path.stat().st_size == 0:
            errors.append(f"missing or empty runtime artifact: {path}")
    if errors:
        for error in errors:
            print(f"[phase12c-target-runtime] ERROR: {error}")
        return 1

    validate_runner_metadata(args.runner_metadata.resolve())
    record = {
        "schemaVersion": 1,
        "recordType": "phase12c-target-runtime",
        "platform": args.platform,
        "implementationState": "TARGET_BUILD_PASS",
        "runtimeResult": "PASS",
        "summary": evidence(summary_path, root),
        "screenshot": evidence(screenshot_path, root),
        "audio": evidence(audio_path, root),
        "runnerMetadata": evidence(args.runner_metadata.resolve(), root),
        "fixture": {
            "id": summary["fixtureId"],
            "version": summary["fixtureVersion"],
            "contentHash": summary["fixtureContentHash"],
        },
        "state": {
            "bytes": summary["stateBytes"],
            "restoredBytes": summary["restoredStateBytes"],
            "sha256": summary["stateSha256"],
            "restoredSha256": summary["restoredStateSha256"],
            "bytesEqual": summary["stateBytesEqual"],
        },
    }
    if args.derived_png is not None:
        derived_png = args.derived_png.resolve()
        record["derivedPng"] = {
            "authoritative": False,
            "derivedFrom": "phase11-clap-editor.ppm",
            "path": derived_png.name,
            "sha256": sha256(derived_png),
        }
    if args.host_log is not None:
        host_log = args.host_log.resolve()
        record["hostLog"] = {"path": host_log.name, "sha256": sha256(host_log)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
