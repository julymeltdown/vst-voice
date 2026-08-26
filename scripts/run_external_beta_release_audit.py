#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.external_beta.release_audit import audit_release, load_json  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the restored-evidence External Beta release audit")
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--archive-manifest", type=Path, required=True)
    parser.add_argument("--archive-root", type=Path, required=True)
    parser.add_argument("--state", choices=("READY", "CLOSED"), default="READY")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    parser.add_argument("--trusted-anchor-sha256")
    args = parser.parse_args(argv)
    try:
        result = audit_release(load_json(args.candidate), load_json(args.archive_manifest), args.archive_root, args.state, trusted_anchor_sha256=args.trusted_anchor_sha256)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "state": args.state, "errors": [str(exc)], "blocked": []}
        print(json.dumps(payload, sort_keys=True))
        return 0 if args.expect_blocked else 2
    text = json.dumps(result.as_dict(), ensure_ascii=False, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
