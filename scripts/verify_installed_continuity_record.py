#!/usr/bin/env python3
"""Replay installed procedural continuity; engineering success cannot authorize release."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.external_beta.installed_continuity_record import audit_installed_continuity_record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("record", "project", "package", "installed-directory", "continuity-directory", "public-key", "voicebank-cli"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("record-sha256", "public-key-sha256", "cli-sha256"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--timeout-seconds", type=float, default=60)
    args = parser.parse_args()
    result = audit_installed_continuity_record(record_path=args.record, record_sha256=args.record_sha256,
        project_path=args.project, package_path=args.package, installed_directory=args.installed_directory,
        continuity_directory=args.continuity_directory, public_key_path=args.public_key,
        public_key_sha256=args.public_key_sha256, cli_path=args.voicebank_cli, cli_sha256=args.cli_sha256,
        timeout_seconds=args.timeout_seconds)
    print(json.dumps(result, sort_keys=True, allow_nan=False))
    return 0 if result["passed"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
