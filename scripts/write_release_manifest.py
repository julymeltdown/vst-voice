#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--status", required=True)
    parser.add_argument("--version", default=os.environ.get("SEAM_VERSION", ""))
    args = parser.parse_args()
    if not args.version:
        parser.error("--version or SEAM_VERSION is required")
    base = args.payload.resolve()
    output = args.output.resolve()
    files = []
    for path in sorted(base.rglob("*")):
        if path.is_file() and path.resolve() != output:
            files.append(
                {
                    "path": path.relative_to(base).as_posix(),
                    "size": path.stat().st_size,
                    "sha256": sha256_file(path),
                }
            )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(
            {"schemaVersion": 1, "version": args.version, "status": args.status, "files": files},
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
