#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.phase13a.distribution_manifest import (  # noqa: E402
    build_wrapper_manifest,
    tree_sha256,
)


def _read_manifest(path: Path) -> dict[str, object]:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"wrapper manifest is not a regular file: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"wrapper manifest must be an object: {path}")
    return value


def _manifest_path(path: Path) -> Path:
    for candidate in (
        path / "wrapper-manifest.json",
        path / "Contents" / "Resources" / "wrapper-manifest.json",
    ):
        if candidate.is_file() and not candidate.is_symlink():
            return candidate
    raise ValueError(f"wrapper manifest is missing: {path}")


def _release_identity(value: object) -> dict[str, str | int] | None:
    if not isinstance(value, dict):
        return None
    identity: dict[str, str | int] = {}
    for key, item in value.items():
        if not isinstance(key, str) or not isinstance(item, (str, int)):
            return None
        identity[key] = item
    return identity


def _refresh(path: Path, clap_sha256: str, platform: str) -> None:
    manifest_path = _manifest_path(path)
    manifest = _read_manifest(manifest_path)
    format_name = manifest.get("format")
    architecture = manifest.get("architecture")
    version = manifest.get("version")
    bundle_identifier = manifest.get("bundleIdentifier")
    release_identity = manifest.get("releaseIdentity")
    if (
        not isinstance(format_name, str)
        or not format_name
        or not isinstance(architecture, str)
        or not architecture
        or not isinstance(version, str)
        or not version
        or not isinstance(bundle_identifier, str)
        or not bundle_identifier
    ):
        raise ValueError(f"wrapper manifest identity is incomplete: {manifest_path}")
    refreshed = build_wrapper_manifest(
        format_name,
        platform,
        architecture,
        version,
        bundle_identifier,
        clap_sha256,
        path,
        _release_identity(release_identity),
    )
    manifest_path.write_text(
        json.dumps(refreshed, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Refresh signed wrapper manifests")
    parser.add_argument("payload", type=Path)
    parser.add_argument("--platform", choices=("macos", "windows"), default="macos")
    args = parser.parse_args(argv)
    try:
        payload = args.payload.resolve()
        clap = payload / "CLAP" / "ProjectSEAMEditor.clap"
        if not clap.exists() or clap.is_symlink():
            raise ValueError(f"canonical CLAP payload is missing or linked: {clap}")
        clap_sha256 = tree_sha256(clap)
        relatives = [Path("VST3") / "ProjectSEAMEditor.vst3"]
        if args.platform == "macos":
            relatives.append(Path("AU") / "ProjectSEAMEditor.component")
        for relative in relatives:
            wrapper = payload / relative
            if wrapper.exists() and not wrapper.is_symlink():
                _refresh(wrapper, clap_sha256, args.platform)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"canonicalClapSha256={clap_sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
