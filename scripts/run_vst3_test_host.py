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

from distribution_manifest import tree_sha256, validate_wrapper_bundle  # noqa: E402


SCENARIO_CHECKS = {"scan", "instantiate", "editorOpen", "editorResize", "editorClose", "process", "stateSave", "stateRestore", "unload", "repeatLifecycle"}


def _tool_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _scenario_errors(path: Path | None) -> list[str]:
    if path is None:
        return []
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"scenario cannot be read: {exc}"]
    if not isinstance(value, dict) or not isinstance(value.get("checks"), list):
        return ["scenario must be an object with a checks array"]
    checks = set(value["checks"])
    unknown = checks - SCENARIO_CHECKS
    if unknown:
        return ["scenario contains unknown checks: " + ", ".join(sorted(map(str, unknown)))]
    missing = {"scan", "instantiate", "process", "unload"} - checks
    return ["scenario is missing required checks: " + ", ".join(sorted(missing))] if missing else []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the pinned Steinberg VST3 test host against an installed package")
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", type=Path)
    parser.add_argument("--platform", choices=["windows", "win32", "macos", "darwin", "linux"])
    parser.add_argument("--expected-sha256")
    parser.add_argument("--canonical-clap-sha256")
    parser.add_argument("--installed-root", type=Path)
    parser.add_argument("--tool-version", default="")
    parser.add_argument("--expected-tool-sha256")
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--host-args", nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    for stale in (args.output / "test-host.log", args.output / "test-host.stderr.log", args.output / "result.json"):
        if stale.is_file() or stale.is_symlink():
            stale.unlink()
    target_platform = (args.platform or platform.system()).lower()
    errors = []
    if not args.host.is_file():
        errors.append("official VST3 test host is missing")
    if not args.plugin.exists():
        errors.append("VST3 package is missing")
    if args.plugin.exists():
        errors.extend(validate_wrapper_bundle("vst3", args.plugin, target_platform, args.canonical_clap_sha256))
        if args.installed_root is not None:
            try:
                args.plugin.resolve().relative_to(args.installed_root.resolve())
            except ValueError:
                errors.append("VST3 package is not below the declared installed root")
        if args.expected_sha256 and tree_sha256(args.plugin) != args.expected_sha256:
            errors.append("VST3 package hash differs from expected installed identity")
    if args.expected_tool_sha256 and args.host.is_file() and _tool_hash(args.host) != args.expected_tool_sha256:
        errors.append("VST3 test host hash differs from the pinned tool identity")
    errors.extend(_scenario_errors(args.scenario))
    status = "NOT_RUN"
    failure_class = ""
    exit_code: int | None = None
    if errors:
        status = "NOT_RUN" if not args.plugin.exists() or not args.host.is_file() else "FAIL"
        failure_class = "tool-or-artifact-missing" if status == "NOT_RUN" else "artifact-contract"
        output = "\n".join(errors) + "\n"
    else:
        environment = os.environ.copy()
        environment["SEAM_VST3_PLUGIN"] = str(args.plugin.resolve())
        environment["SEAM_VST3_SCENARIO"] = str(args.scenario.resolve()) if args.scenario else ""
        started = time.monotonic()
        try:
            process = subprocess.run([str(args.host), *args.host_args], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment, timeout=args.timeout, check=False)
            exit_code = process.returncode
            output = process.stdout
            (args.output / "test-host.stderr.log").write_text(process.stderr, encoding="utf-8")
            status = "PASS" if exit_code == 0 else "FAIL"
            failure_class = "" if status == "PASS" else "test-host-nonzero"
        except subprocess.TimeoutExpired as exc:
            output = (exc.stdout or "") if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", "replace")
            status = "FAIL"
            failure_class = "timeout"
    (args.output / "test-host.log").write_text(output, encoding="utf-8")
    result = {
        "schemaVersion": 1,
        "status": status,
        "failureClass": failure_class,
        "exitCode": exit_code,
        "plugin": str(args.plugin.resolve()) if args.plugin.exists() else str(args.plugin),
        "pluginSha256": tree_sha256(args.plugin) if args.plugin.exists() else "",
        "host": str(args.host.resolve()) if args.host.exists() else str(args.host),
        "hostSha256": _tool_hash(args.host) if args.host.is_file() else "",
        "toolVersion": args.tool_version,
        "scenario": str(args.scenario) if args.scenario else "",
        "platform": target_platform,
        "canonicalClapSha256": args.canonical_clap_sha256 or "",
        "executedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
        "durationMs": int((time.monotonic() - started) * 1000) if "started" in locals() else 0,
    }
    (args.output / "result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if status == "PASS" else 3


if __name__ == "__main__":
    raise SystemExit(main())
