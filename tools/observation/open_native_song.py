#!/usr/bin/env python3
"""Open the retained engineering song after checking its frozen local files."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT = ROOT / "docs/implementation/NATIVE_OBSERVATION_KIT_2026-10-12.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if manifest["scope"] != "ENGINEERING_ONLY" or manifest["releaseEligible"] is not False:
            raise ValueError("Expected the engineering observation manifest")
        checked = set()
        for item in manifest["files"]:
            path = ROOT / item["path"]
            if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
                raise ValueError(f"File escapes the repository: {item['path']}")
            with path.open("rb") as stream:
                digest = hashlib.sha256()
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
                actual = digest.hexdigest()
            if actual != item["sha256"]:
                raise ValueError(f"Frozen file changed: {item['path']}")
            checked.add(item["path"])
        launch = manifest["launch"]
        for key in ("executable", "project", "publicKey"):
            if launch[key] not in checked:
                raise ValueError(f"Unverified launch input: {key}")
        executable = ROOT / launch["executable"]
        bundle = executable.parents[2]
        support = ROOT / launch["applicationSupportRoot"]
        if not support.resolve().is_relative_to(ROOT):
            raise ValueError("Application support root escapes the repository")
        if not args.verify_only:
            if sys.platform != "darwin":
                raise ValueError("This retained session requires macOS")
            working_root = support.parent / "working-copies"
            working_root.mkdir(exist_ok=True)
            working_folder = Path(tempfile.mkdtemp(prefix="session-", dir=working_root))
            working_project = working_folder / Path(launch["project"]).name
            shutil.copy2(ROOT / launch["project"], working_project)
            subprocess.run([
                "open", "-na", str(bundle), "--args",
                "--application-support-root", str(support),
                "--trusted-voicebank-key", str(ROOT / launch["publicKey"]),
                str(working_project),
            ], check=True)
            print(f"Opened working copy: {working_project}")
        print(f"Verified {len(checked)} frozen files; human observation remains NOT_OBSERVED.")
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Observation kit unavailable: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
