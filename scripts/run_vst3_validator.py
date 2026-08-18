#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

from distribution_manifest import tree_sha256, validate_artifact


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
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)

    status = "NOT_RUN"
    exit_code: int | None = None
    log = "validator or plugin missing\n"
    errors = validate_artifact("vst3", args.plugin) if args.plugin.exists() else []
    environment = os.environ.copy()
    resolved_clap_path = ""
    if args.clap_path is not None:
        resolved = args.clap_path.resolve()
        if not resolved.is_dir():
            errors.append(f"CLAP_PATH is not a directory: {resolved}")
        else:
            resolved_clap_path = str(resolved)
            environment["CLAP_PATH"] = resolved_clap_path

    if args.validator.is_file() and args.plugin.exists() and not errors:
        process = subprocess.run(
            [str(args.validator), str(args.plugin)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=environment,
            check=False,
        )
        exit_code = process.returncode
        log = process.stdout
        status = "PASS" if exit_code == 0 else "FAIL"
    elif errors:
        status = "FAIL"
        log = "\n".join(errors) + "\n"

    (args.output / "validator.log").write_text(log, encoding="utf-8")
    result = {
        "status": status,
        "exitCode": exit_code,
        "plugin": str(args.plugin),
        "pluginSha256": tree_sha256(args.plugin) if args.plugin.exists() else "",
        "clapPath": resolved_clap_path,
        "executedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
    }
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if status == "PASS" else 3


if __name__ == "__main__":
    raise SystemExit(main())
