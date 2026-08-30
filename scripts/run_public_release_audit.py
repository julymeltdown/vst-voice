#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.public_release.contracts import PUBLIC_STATES  # noqa: E402
from tools.public_release.release_audit import audit_release, load_json  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Run the restored-evidence Public Production release audit"
    )
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--archive-manifest", type=Path, required=True)
    parser.add_argument("--archive-root", type=Path, required=True)
    parser.add_argument("--state", choices=PUBLIC_STATES, default="PUBLIC_ACTIVE")
    parser.add_argument("--acceptance-contract", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        contract = load_json(args.acceptance_contract) if args.acceptance_contract else None
        result = audit_release(
            load_json(args.candidate),
            load_json(args.archive_manifest),
            args.archive_root,
            args.state,
            acceptance_contract=contract,
        )
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {
            "passed": False,
            "state": args.state,
            "errors": [str(exc)],
            "blocked": [],
        }
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
