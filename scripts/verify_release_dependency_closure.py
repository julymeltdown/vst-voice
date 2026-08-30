#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run scripts/verify_release_dependency_closure.py --help
# 3. Or make executable and run:
#      chmod +x scripts/verify_release_dependency_closure.py && ./scripts/verify_release_dependency_closure.py --help
# ──────────────────

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.phase13a.dependency_closure import (  # noqa: E402
    CommandBinaryInspector,
    dependency_closure_json,
    verify_dependency_closure,
)
from tools.phase13a.release_payload import PayloadPlatform  # noqa: E402


def _openssl_commit(source_root: Path) -> str:
    lock = json.loads(
        (source_root / "phase13a/dependency-lock.json").read_text(encoding="utf-8")
    )
    dependencies = lock.get("dependencies", [])
    for dependency in dependencies:
        if dependency.get("name") == "openssl":
            return str(dependency.get("commit", ""))
    return ""


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify Project SEAM release dependency closure"
    )
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--platform", choices=tuple(PayloadPlatform), required=True)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args(argv)
    try:
        platform = PayloadPlatform(arguments.platform)
        report = verify_dependency_closure(
            arguments.payload,
            platform,
            CommandBinaryInspector(platform),
            _openssl_commit(arguments.source_root),
        )
        output = arguments.output or arguments.payload / "release-dependency-closure.json"
        output.write_text(
            json.dumps(dependency_closure_json(report), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    print(f"RELEASE_DEPENDENCY_CLOSURE={report.status}")
    return 0 if report.status == "PASS" else 3


if __name__ == "__main__":
    raise SystemExit(main())
