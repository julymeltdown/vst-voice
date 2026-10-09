#!/usr/bin/env python3
"""Replay one native project binding. Exit 0 is ENGINEERING_PASS, never release approval."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.external_beta.project_binding_record import audit_project_binding_record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("record", "project", "voicebank-cli"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("record-sha256", "cli-sha256"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--timeout-seconds", type=float, default=60)
    args = parser.parse_args()
    result = audit_project_binding_record(record_path=args.record, record_sha256=args.record_sha256,
        project_path=args.project, cli_path=args.voicebank_cli, cli_sha256=args.cli_sha256,
        timeout_seconds=args.timeout_seconds)
    print(json.dumps(result, sort_keys=True))
    return 0 if result["passed"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
