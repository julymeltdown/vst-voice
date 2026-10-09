#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run scripts/assemble_release_payload.py --help
# 3. Or make executable and run:
#      chmod +x scripts/assemble_release_payload.py && ./scripts/assemble_release_payload.py --help
# ──────────────────

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.phase13a.release_payload import (  # noqa: E402
    PayloadAssemblyError,
    PayloadPlatform,
    assemble_release_payload,
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Assemble one identity-bound Project SEAM release payload"
    )
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--platform", choices=tuple(PayloadPlatform), required=True)
    arguments = parser.parse_args(argv)
    try:
        result = assemble_release_payload(
            arguments.payload,
            arguments.source_root,
            PayloadPlatform(arguments.platform),
        )
    except PayloadAssemblyError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 3
    print(f"RELEASE_PAYLOAD_MANIFEST={result.path}")
    print(f"RELEASE_PAYLOAD_SHA256={result.payload_sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
