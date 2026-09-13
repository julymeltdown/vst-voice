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
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.phase13a.release_payload import (  # noqa: E402
    PayloadAssemblyError,
    PayloadPlatform,
    assemble_release_payload,
)
from tools.phase13a.neural_helper_staging import stage_neural_helper  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Assemble one identity-bound Project SEAM release payload"
    )
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--platform", choices=tuple(PayloadPlatform), required=True)
    parser.add_argument(
        "--neural-worker",
        type=Path,
        help="Finalized neural worker to stage beside each payload surface",
    )
    parser.add_argument(
        "--neural-dependency",
        type=Path,
        action="append",
        default=[],
        help="Runtime dependency staged beside the worker, such as the inference library",
    )
    parser.add_argument(
        "--neural-surface",
        action="append",
        default=None,
        help="Restrict staging to named surfaces; the default stages every surface",
    )
    parser.add_argument("--neural-protocol-version", type=int, choices=(1, 2), default=1)
    arguments = parser.parse_args(argv)
    try:
        if arguments.neural_worker is not None:
            try:
                build_id = _staged_build_id(arguments.payload)
            except (OSError, ValueError) as error:
                print(
                    f"ERROR: the payload build identity is unreadable: {error}",
                    file=sys.stderr,
                )
                return 3
            stage_neural_helper(
                arguments.payload,
                PayloadPlatform(arguments.platform),
                arguments.neural_worker,
                tuple(arguments.neural_dependency),
                build_id,
                surface_ids=(
                    tuple(arguments.neural_surface)
                    if arguments.neural_surface
                    else None
                ),
                protocol_version=arguments.neural_protocol_version,
            )
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


def _staged_build_id(payload: Path) -> str:
    """Read the payload's own build identity before the manifest is sealed."""
    identity = json.loads((payload / "RELEASE_IDENTITY.json").read_text(encoding="utf-8"))
    build_id = identity.get("buildId")
    if not isinstance(build_id, str) or not build_id:
        raise PayloadAssemblyError(("payload identity has no build id",))
    return build_id


if __name__ == "__main__":
    raise SystemExit(main())
