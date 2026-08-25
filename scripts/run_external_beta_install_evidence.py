#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.external_beta.install_evidence import load_json, validate_install_record  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate one External Beta clean-install lifecycle record")
    parser.add_argument("--matrix", type=Path, default=ROOT / "docs/product/external-beta-install-matrix.json")
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = validate_install_record(load_json(args.record), load_json(args.matrix), args.evidence_root)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "errors": [str(exc)], "blocked": []}
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
