#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

from distribution_manifest import tree_sha256, validate_artifact, validate_wrapper_bundle  # noqa: E402


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _tool_identity(path: Path) -> dict[str, str]:
    try:
        return {"path": str(path.resolve()), "sha256": _sha256(path)}
    except OSError:
        return {"path": str(path), "sha256": ""}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run Steinberg vst3-validator and preserve raw evidence")
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--clap-path",
        type=Path,
        help="Directory containing the canonical CLAP module used by clap-wrapper VST3 builds",
    )
    parser.add_argument("--platform", choices=["windows", "macos", "darwin", "linux"])
    parser.add_argument("--expected-sha256")
    parser.add_argument("--canonical-clap-sha256")
    parser.add_argument("--installed-root", type=Path)
    parser.add_argument("--tool-version", default="")
    parser.add_argument("--expected-tool-sha256")
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    for stale in (args.output / "validator.log", args.output / "validator.stderr.log", args.output / "result.json"):
        if stale.is_file() or stale.is_symlink():
            stale.unlink()
    (args.output / "validator.stderr.log").write_text("", encoding="utf-8")

    status = "NOT_RUN"
    exit_code: int | None = None
    failure_class = ""
    log = "validator or plugin missing\n"
    if args.platform:
        target_platform = args.platform.lower()
    elif (args.plugin / "Contents" / "MacOS").is_dir():
        target_platform = "macos"
    elif any(args.plugin.glob("**/*-linux/*")):
        target_platform = "linux"
    else:
        target_platform = platform.system().lower()
    errors = validate_artifact("vst3", args.plugin, target_platform) if args.plugin.exists() else ["plugin artifact does not exist"]
    environment = os.environ.copy()
    resolved_clap_path = ""
    if args.clap_path is not None:
        resolved = args.clap_path.resolve()
        if not resolved.is_dir():
            errors.append(f"CLAP_PATH is not a directory: {resolved}")
        else:
            resolved_clap_path = str(resolved)
            environment["CLAP_PATH"] = resolved_clap_path
    if args.installed_root is not None and args.plugin.exists():
        installed_root = args.installed_root.resolve()
        try:
            args.plugin.resolve().relative_to(installed_root)
        except ValueError:
            errors.append("plugin is not below the declared installed root")
    if args.expected_sha256 and args.plugin.exists():
        try:
            if tree_sha256(args.plugin) != args.expected_sha256:
                errors.append("plugin tree hash differs from expected installed identity")
        except ValueError as exc:
            errors.append(str(exc))
    if args.plugin.exists() and not errors:
        manifest_candidates = (
            args.plugin / "wrapper-manifest.json",
            args.plugin / "Contents" / "Resources" / "wrapper-manifest.json",
        )
        if args.canonical_clap_sha256 or any(path.is_file() for path in manifest_candidates):
            errors.extend(validate_wrapper_bundle("vst3", args.plugin, target_platform, args.canonical_clap_sha256))

    if args.validator.is_file() and args.plugin.exists() and not errors:
        if args.expected_tool_sha256 and _sha256(args.validator) != args.expected_tool_sha256:
            errors.append("validator tool hash differs from the pinned tool identity")
    if args.validator.is_file() and args.plugin.exists() and not errors:
        started = time.monotonic()
        try:
            process = subprocess.run([str(args.validator), str(args.plugin)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment, check=False, timeout=args.timeout)
            exit_code = process.returncode
            log = process.stdout
            (args.output / "validator.stderr.log").write_text(process.stderr, encoding="utf-8")
            status = "PASS" if exit_code == 0 else "FAIL"
            failure_class = "" if status == "PASS" else "validator-nonzero"
        except subprocess.TimeoutExpired as exc:
            status = "FAIL"
            failure_class = "timeout"
            log = (exc.stdout or "") if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", "replace")
            exit_code = None
    elif not args.validator.is_file():
        status = "NOT_RUN"
        failure_class = "tool-missing"
    elif errors:
        status = "FAIL"
        failure_class = "artifact-contract"
        log = "\n".join(errors) + "\n"

    (args.output / "validator.log").write_text(log, encoding="utf-8")
    result = {
        "schemaVersion": 1,
        "status": status,
        "failureClass": failure_class,
        "exitCode": exit_code,
        "plugin": str(args.plugin),
        "installedPath": str(args.plugin.resolve()) if args.plugin.exists() else "",
        "pluginSha256": tree_sha256(args.plugin) if args.plugin.exists() else "",
        "clapPath": resolved_clap_path,
        "canonicalClapSha256": args.canonical_clap_sha256 or "",
        "tool": _tool_identity(args.validator) if args.validator.is_file() else {"path": str(args.validator), "sha256": ""},
        "toolVersion": args.tool_version,
        "platform": target_platform,
        "executedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        "durationMs": int((time.monotonic() - started) * 1000) if 'started' in locals() else 0,
    }
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if status == "PASS" else 3


if __name__ == "__main__":
    raise SystemExit(main())
