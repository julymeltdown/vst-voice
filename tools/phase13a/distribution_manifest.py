#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import platform as platform_module
from pathlib import Path
from typing import Any

MANDATORY_VALIDATIONS = {"vst3-validator", "auval"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_sha256(path: Path) -> str:
    path = Path(path)
    if path.is_symlink():
        raise ValueError(f"symbolic-link artifacts are forbidden: {path}")
    if path.is_file():
        return sha256_file(path)
    if not path.is_dir():
        raise ValueError(f"artifact path is not a file or directory: {path}")
    digest = hashlib.sha256()
    for item in sorted(path.rglob("*")):
        if item.is_symlink():
            raise ValueError(f"symbolic-link artifacts are forbidden: {item}")
        if not item.is_file():
            continue
        relative = item.relative_to(path).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(bytes.fromhex(sha256_file(item)))
    return digest.hexdigest()


def sha256_path(path: Path) -> str:
    return tree_sha256(path)


def _files(root: Path) -> list[Path]:
    return sorted(path for path in Path(root).rglob("*") if path.is_file())


def validate_artifact(kind: str, path: Path, platform: str | None = None) -> list[str]:
    kind = kind.lower()
    path = Path(path)
    platform = (platform or platform_module.system()).lower()
    errors: list[str] = []
    if not path.exists():
        return [f"{kind}: artifact does not exist: {path}"]
    if path.is_symlink():
        return [f"{kind}: symbolic-link artifacts are forbidden"]

    if kind == "vst3":
        if path.suffix.lower() != ".vst3":
            errors.append("vst3: artifact must have a .vst3 name")
        if path.is_file():
            if platform not in {"windows", "win32"}:
                errors.append("vst3: a single-file artifact is accepted only on Windows")
            elif path.stat().st_size == 0:
                errors.append("vst3: Windows single-file artifact is empty")
        elif path.is_dir():
            binaries = [
                file
                for file in _files(path)
                if file.suffix.lower() in {".so", ".dll", ".dylib", ".vst3"}
                or "MacOS" in file.parts
            ]
            if not binaries:
                errors.append("vst3: bundle contains no platform binary")
        else:
            errors.append("vst3: artifact must be a file or folder bundle")
    elif kind in {"auv2", "au"}:
        if platform not in {"darwin", "macos"}:
            errors.append("AUv2 validation must run on macOS")
        if path.suffix.lower() != ".component":
            errors.append("auv2: artifact must have a .component name")
        binary_dir = path / "Contents" / "MacOS"
        if not binary_dir.is_dir() or not any(item.is_file() for item in binary_dir.iterdir()):
            errors.append("auv2: bundle contains no Contents/MacOS binary")
        if not (path / "Contents" / "Info.plist").is_file():
            errors.append("auv2: bundle is missing Contents/Info.plist")
    elif kind == "clap":
        if path.suffix.lower() != ".clap":
            errors.append("clap: artifact must have a .clap name")
        if path.is_file() and path.stat().st_size == 0:
            errors.append("clap: module is empty")
        if path.is_dir():
            binary_dir = path / "Contents" / "MacOS"
            if not binary_dir.is_dir() or not any(binary_dir.glob("*")):
                errors.append("clap: bundle contains no platform binary")
    else:
        errors.append(f"unsupported artifact kind: {kind}")
    return errors


def build_release_manifest(
    version: str,
    artifacts: list[dict[str, Any]],
    validations: dict[str, str],
) -> dict[str, Any]:
    unresolved = sorted(
        name for name in MANDATORY_VALIDATIONS if validations.get(name) != "PASS"
    )
    return {
        "schemaVersion": 1,
        "product": "Project SEAM",
        "version": version,
        "artifacts": artifacts,
        "validations": validations,
        "unresolvedMandatory": unresolved,
        "unresolvedMandatoryCount": len(unresolved),
        "releaseStatus": "READY" if not unresolved else "BLOCKED",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--kind", choices=["clap", "vst3", "auv2"], required=True)
    validate.add_argument("--path", type=Path, required=True)
    validate.add_argument("--platform")
    args = parser.parse_args(argv)
    errors = validate_artifact(args.kind, args.path, args.platform)
    if errors:
        for error in errors:
            print("ERROR:", error)
        return 3
    print(f"PHASE13A_{args.kind.upper()}_ARTIFACT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
