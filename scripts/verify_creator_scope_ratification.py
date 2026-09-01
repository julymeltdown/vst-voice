#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["jsonschema==4.26.0"]
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly:
#      uv run scripts/verify_creator_scope_ratification.py --root .
# 3. Or use the repository Python:
#      python3 scripts/verify_creator_scope_ratification.py --root .
# ──────────────────

from __future__ import annotations

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.creator_scope.verifier import verify_repository  # noqa: E402


class Arguments(argparse.Namespace):
    root: Path = ROOT


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify the Project SEAM creator scope ratification contract"
    )
    _ = parser.add_argument("--root", type=Path, default=ROOT)
    arguments = parser.parse_args(argv, namespace=Arguments())
    result = verify_repository(arguments.root)
    if result.errors:
        for error in result.errors:
            print(f"ERROR: {error}", file=sys.stderr)
        message = (
            f"CREATOR_SCOPE_CONTRACT=FAIL state={result.state} schema8_authorized=false"
        )
        print(message, file=sys.stderr)
        return 1
    authorized = "true" if result.schema8_authorized else "false"
    message = f"CREATOR_SCOPE_CONTRACT=PASS state={result.state} schema8_authorized={authorized}"
    print(message)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
