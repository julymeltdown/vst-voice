from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


PROJECT_VERSION = re.compile(
    r"project\(\s*ProjectSEAM\s+VERSION\s+([0-9]+(?:\.[0-9]+){2}(?:-[A-Za-z0-9.-]+)?)\b",
    re.IGNORECASE,
)
HEX40 = re.compile(r"[0-9a-fA-F]{40}")
BUILD_ID = re.compile(r"[A-Za-z0-9._-]{4,160}")


class ReleaseIdentityInputError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class ReleaseIdentity:
    version: str
    build_id: str
    source_commit: str
    build_epoch: int

    def as_dict(self) -> dict[str, str | int]:
        return {
            "product": "Project SEAM",
            "version": self.version,
            "buildId": self.build_id,
            "sourceCommit": self.source_commit,
            "buildEpoch": self.build_epoch,
        }


def read_project_version(source_root: Path) -> str:
    cmake = Path(source_root) / "CMakeLists.txt"
    match = PROJECT_VERSION.search(cmake.read_text(encoding="utf-8"))
    if match is None:
        raise ReleaseIdentityInputError(
            f"ProjectSEAM project version is missing from {cmake}"
        )
    return match.group(1)


def _git_value(source_root: Path, arguments: tuple[str, ...]) -> str:
    process = subprocess.run(
        ["git", "-C", str(source_root), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return process.stdout.strip() if process.returncode == 0 else ""


def resolve_release_identity(
    source_root: Path,
    environment: Mapping[str, str] | None = None,
) -> ReleaseIdentity:
    root = Path(source_root).resolve()
    values = os.environ if environment is None else environment
    version = read_project_version(root)
    configured_version = values.get("SEAM_VERSION") or values.get(
        "PROJECT_SEAM_VERSION"
    )
    if configured_version is not None and configured_version != version:
        raise ReleaseIdentityInputError(
            f"configured release version {configured_version} does not match source version {version}"
        )

    source_commit = values.get("SEAM_SOURCE_COMMIT") or _git_value(
        root, ("rev-parse", "HEAD")
    )
    if HEX40.fullmatch(source_commit) is None:
        raise ReleaseIdentityInputError(
            "SEAM_SOURCE_COMMIT or the source checkout must provide a 40-character commit"
        )

    build_id = values.get("SEAM_BUILD_ID") or f"{version}-{source_commit[:12]}"
    if BUILD_ID.fullmatch(build_id) is None:
        raise ReleaseIdentityInputError(
            "SEAM_BUILD_ID must contain 4-160 portable identifier characters"
        )

    epoch_text = values.get("SEAM_BUILD_EPOCH") or _git_value(
        root, ("show", "-s", "--format=%ct", source_commit)
    )
    if not epoch_text.isdigit():
        raise ReleaseIdentityInputError(
            "SEAM_BUILD_EPOCH or the source checkout must provide a non-negative epoch"
        )
    return ReleaseIdentity(version, build_id, source_commit.lower(), int(epoch_text))


def write_release_identity(path: Path, identity: ReleaseIdentity) -> None:
    Path(path).write_text(
        json.dumps(identity.as_dict(), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Read the canonical Project SEAM source release identity"
    )
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument(
        "--field", choices=("version", "build-id", "source-commit", "build-epoch")
    )
    args = parser.parse_args(argv)
    identity = resolve_release_identity(args.source_root)
    fields = {
        "version": identity.version,
        "build-id": identity.build_id,
        "source-commit": identity.source_commit,
        "build-epoch": str(identity.build_epoch),
    }
    print(fields[args.field] if args.field is not None else json.dumps(identity.as_dict(), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
