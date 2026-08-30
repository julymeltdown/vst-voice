#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run scripts/verify_production_signing_input.py --help
# 3. Or make executable and run:
#      chmod +x scripts/verify_production_signing_input.py && ./scripts/verify_production_signing_input.py --help
# ──────────────────

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.phase13a.signing_eligibility import (  # noqa: E402
    production_payload_issues,
    production_source_trust_issues,
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Block production credentials from development-trust inputs"
    )
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--source-root", type=Path)
    inputs.add_argument("--payload", type=Path)
    arguments = parser.parse_args(argv)
    issues = (
        production_source_trust_issues(arguments.source_root)
        if arguments.source_root is not None
        else production_payload_issues(arguments.payload)
    )
    if issues:
        print("PRODUCTION_SIGNING_INPUT=BLOCKED", file=sys.stderr)
        for issue in issues:
            print(f"error={issue}", file=sys.stderr)
        return 4
    print("PRODUCTION_SIGNING_INPUT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
