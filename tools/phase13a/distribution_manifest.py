#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import platform as platform_module
from pathlib import Path
from typing import Any

MANDATORY_VALIDATIONS = {"vst3-validator", "auval"}
VALIDATION_STATUSES = {"PASS", "FAIL", "NOT_RUN", "BLOCKED"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_sha256(path: Path, exclude: set[str] | None = None) -> str:
    path = Path(path)
    excluded = exclude or set()
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
        if relative.decode("utf-8") in excluded:
            continue
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(bytes.fromhex(sha256_file(item)))
    return digest.hexdigest()


def sha256_path(path: Path) -> str:
    return tree_sha256(path)


def read_validation_status(
    path: Path,
    *,
    expected_artifact_sha256: str,
    artifact_hash_field: str,
) -> str:
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"validation result is not a regular file: {path}")
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"validation result cannot be read: {path}: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("schemaVersion") != 1:
        raise ValueError(f"validation result schema is invalid: {path}")
    status = payload.get("status")
    if status not in VALIDATION_STATUSES:
        raise ValueError(f"validation result status is invalid: {path}")
    if status == "PASS" and payload.get(artifact_hash_field) != expected_artifact_sha256:
        raise ValueError(
            f"validation PASS is bound to a different artifact: {path}"
        )
    return status


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
            errors.append("vst3: single-file artifacts are forbidden; use a package-shaped folder")
        elif path.is_dir():
            binaries = [
                file
                for file in _files(path)
                if file.suffix.lower() in {".so", ".dll", ".dylib", ".vst3"}
                or "MacOS" in file.parts
            ]
            if not binaries:
                errors.append("vst3: bundle contains no platform binary")
            if platform in {"windows", "win32"}:
                if (path / "moduleinfo.json").is_symlink() or not (path / "moduleinfo.json").is_file():
                    errors.append("vst3: Windows folder package requires moduleinfo.json")
                if any(item.is_file() and item.suffix.lower() in {".dll", ".vst3"} for item in path.iterdir()):
                    errors.append("vst3: Windows executable must be nested below an architecture directory")
            if platform in {"darwin", "macos"}:
                if not (path / "Contents" / "Info.plist").is_file():
                    errors.append("vst3: macOS bundle is missing Contents/Info.plist")
                if not (path / "Contents" / "MacOS").is_dir() or not any((path / "Contents" / "MacOS").iterdir()):
                    errors.append("vst3: macOS bundle is missing Contents/MacOS binary")
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


def _wrapper_manifest_path(path: Path) -> Path:
    candidates = [
        path / "wrapper-manifest.json",
        path / "Contents" / "Resources" / "wrapper-manifest.json",
    ]
    for candidate in candidates:
        if candidate.is_file() and not candidate.is_symlink():
            return candidate
    raise ValueError(f"wrapper manifest is missing below {path}")


def _wrapper_manifest_exclusions(path: Path, platform: str) -> set[str]:
    exclusions = {"wrapper-manifest.json", "Contents/Resources/wrapper-manifest.json"}
    if platform.lower() not in {"darwin", "macos"}:
        return exclusions
    for item in path.rglob("*"):
        if not item.is_file() or item.is_symlink():
            continue
        relative = item.relative_to(path).as_posix()
        parts = item.relative_to(path).parts
        if "_CodeSignature" in parts or any(
            parts[index:index + 2] == ("Contents", "MacOS")
            for index in range(len(parts) - 1)
        ):
            exclusions.add(relative)
    return exclusions


def _validate_wrapper_manifest(
    path: Path,
    manifest: dict[str, Any],
    canonical_clap_sha256: str | None,
    platform: str,
) -> list[str]:
    errors: list[str] = []
    if manifest.get("schemaVersion") != 1 or manifest.get("product") != "Project SEAM":
        errors.append("wrapper manifest schema/product is invalid")
    if manifest.get("format") not in {"VST3", "AUv2"}:
        errors.append("wrapper manifest format is invalid")
    digest = manifest.get("canonicalClapSha256")
    if not isinstance(digest, str) or len(digest) != 64:
        errors.append("wrapper manifest canonicalClapSha256 is invalid")
    elif canonical_clap_sha256 is not None and digest != canonical_clap_sha256:
        errors.append("wrapper manifest canonical CLAP hash differs from the build input")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        errors.append("wrapper manifest requires file hashes")
        files = []
    for item in files:
        if not isinstance(item, dict):
            errors.append("wrapper manifest file entry must be an object")
            continue
        relative = item.get("path")
        if not isinstance(relative, str) or Path(relative).is_absolute() or ".." in Path(relative).parts:
            errors.append("wrapper manifest file path is unsafe")
            continue
        candidate = (path / relative).resolve()
        try:
            candidate.relative_to(path.resolve())
        except ValueError:
            errors.append("wrapper manifest file path escapes the wrapper")
            continue
        if candidate.is_symlink() or not candidate.is_file():
            errors.append(f"wrapper manifest file is missing or linked: {relative}")
            continue
        if item.get("size") != candidate.stat().st_size or item.get("sha256") != sha256_file(candidate):
            errors.append(f"wrapper manifest file hash mismatch: {relative}")
    declared_tree = manifest.get("wrapperTreeSha256")
    if not isinstance(declared_tree, str) or len(declared_tree) != 64:
        errors.append("wrapper manifest wrapperTreeSha256 is invalid")
    elif declared_tree != tree_sha256(path, _wrapper_manifest_exclusions(path, platform)):
        errors.append("wrapper manifest tree hash mismatch")
    return errors


def validate_wrapper_bundle(kind: str, path: Path, platform: str, canonical_clap_sha256: str | None = None) -> list[str]:
    errors = validate_artifact(kind, path, platform)
    if errors:
        return errors
    path = Path(path)
    try:
        manifest_path = _wrapper_manifest_path(path)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(manifest, dict):
            return ["wrapper manifest root must be an object"]
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return [f"wrapper manifest cannot be read: {exc}"]
    errors.extend(_validate_wrapper_manifest(path, manifest, canonical_clap_sha256, platform))
    manifest_format = "AUv2" if kind.lower() in {"au", "auv2"} else "VST3"
    expected_shape = {
        ("VST3", "windows"): "windows-vst3-folder",
        ("VST3", "win32"): "windows-vst3-folder",
        ("VST3", "darwin"): "macos-vst3-bundle",
        ("VST3", "macos"): "macos-vst3-bundle",
        ("VST3", "linux"): "linux-vst3-bundle",
        ("AUv2", "darwin"): "macos-auv2-component",
        ("AUv2", "macos"): "macos-auv2-component",
    }.get((manifest_format, platform.lower()))
    if expected_shape and manifest.get("packageShape") != expected_shape:
        errors.append(f"wrapper manifest packageShape must be {expected_shape}")
    return errors


def build_wrapper_manifest(format_name: str, platform: str, architecture: str, version: str, bundle_identifier: str, canonical_clap_sha256: str, wrapper_path: Path) -> dict[str, Any]:
    wrapper_path = Path(wrapper_path)
    normalized_format = "AUv2" if format_name.lower() in {"au", "auv2"} else "VST3"
    normalized_platform = "macos" if platform.lower() in {"darwin", "macos"} else platform.lower()
    exclusions = _wrapper_manifest_exclusions(wrapper_path, normalized_platform)
    files: list[dict[str, Any]] = []
    for item in sorted(wrapper_path.rglob("*")):
        if item.is_symlink() or not item.is_file():
            continue
        relative = item.relative_to(wrapper_path).as_posix()
        if relative in exclusions:
            continue
        files.append({"path": relative, "size": item.stat().st_size, "sha256": sha256_file(item)})
    shape = {("VST3", "windows"): "windows-vst3-folder", ("VST3", "win32"): "windows-vst3-folder", ("VST3", "darwin"): "macos-vst3-bundle", ("VST3", "macos"): "macos-vst3-bundle", ("VST3", "linux"): "linux-vst3-bundle", ("AUv2", "darwin"): "macos-auv2-component", ("AUv2", "macos"): "macos-auv2-component"}.get((normalized_format, platform.lower()), "")
    return {
        "schemaVersion": 1,
        "product": "Project SEAM",
        "format": normalized_format,
        "platform": normalized_platform,
        "architecture": architecture,
        "canonicalClapSha256": canonical_clap_sha256,
        "wrapperTreeSha256": tree_sha256(wrapper_path, exclusions),
        "bundleIdentifier": bundle_identifier,
        "version": version,
        "packageShape": shape,
        "mutableSignaturePaths": sorted(exclusions - {"wrapper-manifest.json", "Contents/Resources/wrapper-manifest.json"}),
        "files": files,
    }


def build_release_manifest(
    version: str,
    artifacts: list[dict[str, Any]],
    validations: dict[str, str],
    validation_evidence: dict[str, str] | None = None,
) -> dict[str, Any]:
    unresolved = sorted(
        name for name in MANDATORY_VALIDATIONS if validations.get(name) != "PASS"
    )
    manifest = {
        "schemaVersion": 1,
        "product": "Project SEAM",
        "version": version,
        "artifacts": artifacts,
        "validations": validations,
        "unresolvedMandatory": unresolved,
        "unresolvedMandatoryCount": len(unresolved),
        "releaseStatus": "READY" if not unresolved else "BLOCKED",
    }
    if validation_evidence:
        manifest["validationEvidence"] = dict(sorted(validation_evidence.items()))
    return manifest


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
