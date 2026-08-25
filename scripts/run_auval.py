#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import platform
import plistlib
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

from distribution_manifest import tree_sha256, validate_wrapper_bundle  # noqa: E402


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def discover_component(component: Path | None) -> tuple[Path | None, dict[str, str], list[str]]:
    candidates: list[Path] = []
    if component is not None:
        candidates.append(component)
    candidates.extend([
        Path("/Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component"),
        Path.home() / "Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component",
    ])
    errors: list[str] = []
    for candidate in candidates:
        if not candidate.exists():
            continue
        if candidate.is_symlink() or not candidate.is_dir() or candidate.suffix.lower() != ".component":
            errors.append(f"component is not an installed package-shaped directory: {candidate}")
            continue
        plist_path = candidate / "Contents" / "Info.plist"
        if not plist_path.is_file() or plist_path.is_symlink():
            errors.append("installed AUv2 component is missing Contents/Info.plist")
            continue
        try:
            value = plistlib.loads(plist_path.read_bytes())
        except (OSError, plistlib.InvalidFileException) as exc:
            errors.append(f"installed AUv2 Info.plist cannot be read: {exc}")
            continue
        entries = value.get("AudioComponents", [])
        entry = entries[0] if isinstance(entries, list) and entries and isinstance(entries[0], dict) else value
        discovered = {key: str(entry.get(key, "")) for key in ("type", "subtype", "manufacturer")}
        if not all(discovered.values()):
            errors.append("installed AUv2 metadata does not declare type, subtype, and manufacturer")
            continue
        return candidate, discovered, errors
    return None, {}, errors or ["installed AUv2 component was not found"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run auval against an installed Project SEAM AUv2 component")
    parser.add_argument("--component", type=Path)
    parser.add_argument("--type")
    parser.add_argument("--subtype")
    parser.add_argument("--manufacturer")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--platform", choices=["macos", "darwin", "windows", "linux"])
    parser.add_argument("--expected-sha256")
    parser.add_argument("--installed-root", type=Path)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--expected-tool-sha256")
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    for stale in (args.output / "auval.log", args.output / "auval.stderr.log", args.output / "result.json"):
        if stale.is_file() or stale.is_symlink():
            stale.unlink()
    status = "NOT_RUN"
    exit_code: int | None = None
    failure_class = ""
    log = "auval requires macOS and an installed component\n"
    target_platform = (args.platform or platform.system()).lower()
    component, discovered, errors = discover_component(args.component)
    if target_platform not in {"darwin", "macos"}:
        errors.append("AUv2 validation requires macOS")
    if component is not None:
        if args.installed_root is not None:
            try:
                component.resolve().relative_to(args.installed_root.resolve())
            except ValueError:
                errors.append("component is not below the declared installed root")
        for field in ("type", "subtype", "manufacturer"):
            requested = getattr(args, field)
            if requested and requested != discovered.get(field):
                errors.append(f"AUv2 {field} does not match installed metadata")
        if args.expected_sha256:
            try:
                if tree_sha256(component) != args.expected_sha256:
                    errors.append("component tree hash differs from expected installed identity")
            except ValueError as exc:
                errors.append(str(exc))
        if not errors:
            errors.extend(validate_wrapper_bundle("auv2", component, target_platform, None))
    auval_path = shutil.which("auval")
    if args.expected_tool_sha256 and auval_path and _sha256(Path(auval_path)) != args.expected_tool_sha256:
        errors.append("auval tool hash differs from the pinned tool identity")
    if not errors and auval_path:
        started = time.monotonic()
        try:
            process = subprocess.run([auval_path, "-v", discovered["type"], discovered["subtype"], discovered["manufacturer"]], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=args.timeout, check=False)
            exit_code = process.returncode
            log = process.stdout
            (args.output / "auval.stderr.log").write_text(process.stderr, encoding="utf-8")
            status = "PASS" if exit_code == 0 else "FAIL"
            failure_class = "" if status == "PASS" else "auval-nonzero"
        except subprocess.TimeoutExpired as exc:
            status = "FAIL"
            failure_class = "timeout"
            log = (exc.stdout or "") if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", "replace")
    elif errors:
        status = "FAIL" if component is not None else "NOT_RUN"
        failure_class = "artifact-contract" if component is not None else "component-missing"
        log = "\n".join(errors) + "\n"
    else:
        status = "NOT_RUN"
        failure_class = "tool-missing"
    (args.output / "auval.log").write_text(log, encoding="utf-8")
    result = {
        "schemaVersion": 1,
        "status": status,
        "failureClass": failure_class,
        "exitCode": exit_code,
        "component": str(component) if component is not None else "",
        "componentSha256": tree_sha256(component) if component is not None and component.exists() else "",
        "discovered": discovered,
        "platform": target_platform,
        "executedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        "durationMs": int((time.monotonic() - started) * 1000) if "started" in locals() else 0,
        "tool": {"path": auval_path or "", "sha256": _sha256(Path(auval_path)) if auval_path else ""},
    }
    (args.output / "result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if status == "PASS" else 3


if __name__ == "__main__":
    raise SystemExit(main())
