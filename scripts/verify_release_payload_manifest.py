#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run scripts/verify_release_payload_manifest.py --help
# 3. Or make executable and run:
#      chmod +x scripts/verify_release_payload_manifest.py && ./scripts/verify_release_payload_manifest.py --help
# ──────────────────

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.phase13a.payload_manifest import (  # noqa: E402
    verify_release_payload_manifest,
)
from tools.phase13a.release_payload import (  # noqa: E402
    PayloadAssemblyError,
    PayloadPlatform,
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify and query a sealed Project SEAM release payload"
    )
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--platform", choices=tuple(PayloadPlatform), required=True)
    parser.add_argument(
        "--field",
        choices=("root", "version", "build-id", "source-commit", "manifest"),
    )
    arguments = parser.parse_args(argv)
    try:
        verified = verify_release_payload_manifest(
            arguments.payload, PayloadPlatform(arguments.platform)
        )
    except (OSError, UnicodeError, json.JSONDecodeError, PayloadAssemblyError) as error:
        print(f"RELEASE_PAYLOAD=BLOCKED\nerror={error}", file=sys.stderr)
        return 3
    fields = {
        "root": str(verified.payload_root),
        "version": verified.identity.version,
        "build-id": verified.identity.build_id,
        "source-commit": verified.identity.source_commit,
        "manifest": str(verified.manifest_path),
    }
    if arguments.field is None:
        print("RELEASE_PAYLOAD=PASS")
    else:
        print(fields[arguments.field])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
