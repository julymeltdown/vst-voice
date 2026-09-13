"""Build runtime-compatible neural package metadata from finalized payload bytes.

This does not qualify dependencies or authenticate the resulting manifest. The
release owner must bind its digest into the trusted surface release identity.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from tools.phase13a.payload_paths import (
    PayloadAssemblyError,
    require_payload_path,
    require_real_directory,
)
from tools.phase13a.payload_surfaces import PayloadPlatform, Surface, surface_matrix

MAXIMUM_FILE_BYTES = 256 * 1024 * 1024
HELPER_MANIFEST_NAME = "neural-helper-package.json"


def build_neural_deployment_descriptor(
    package_root: Path, build_id: str, platform: PayloadPlatform, surface: str,
    module: str, manifest_path: str, *, protocol_version: int = 1,
) -> tuple[bytes, str]:
    """Return unsigned canonical bytes for the release owner's detached signature.

    Rebuild the package manifest from finalized files before binding its digest.
    This does not certify a worker implementation or grant release approval.
    """
    if type(protocol_version) is not int or protocol_version not in (1, 2):
        raise PayloadAssemblyError(("neural deployment protocol is unsupported",))
    if (platform not in (PayloadPlatform.MACOS_ARM64, PayloadPlatform.WINDOWS_X64)
            or surface not in {"standalone", "clap", "vst3", "auv2"}
            or (surface == "auv2" and platform != PayloadPlatform.MACOS_ARM64)):
        raise PayloadAssemblyError(("neural deployment platform or surface is unsupported",))
    if (not manifest_path or len(manifest_path.encode("utf-8")) > 4096
            or any(ord(c) < 32 or ord(c) == 127 for c in manifest_path)
            or "\\" in manifest_path or ":" in manifest_path
            or any(part in {"", ".", ".."} for part in manifest_path.split("/"))
            or manifest_path == module):
        raise PayloadAssemblyError(("neural deployment manifest path is invalid",))
    root = require_real_directory(package_root, "neural package root")
    path = require_payload_path(root, manifest_path)
    if not path.is_file() or path.stat().st_size > 256 * 1024:
        raise PayloadAssemblyError(("neural deployment manifest exceeds bounds",))
    with path.open("rb") as stream:
        raw = stream.read(256 * 1024 + 1)
    if len(raw) > 256 * 1024:
        raise PayloadAssemblyError(("neural deployment manifest exceeds bounds",))
    try:
        value = json.loads(raw)
        if (value["buildId"] != build_id or value["module"]["path"] != module
                or value["protocolVersion"] != protocol_version):
            raise ValueError("package identity differs from deployment target")
        expected, digest = build_neural_package_manifest(
            root, build_id, module, value["helper"]["path"],
            tuple(entry["path"] for entry in value["dependencies"]),
            protocol_version=protocol_version)
        if raw != expected:
            raise ValueError("package manifest differs from finalized files")
    except (ValueError, TypeError, KeyError, AttributeError, RecursionError) as error:
        raise PayloadAssemblyError(("neural deployment package is invalid",)) from error
    descriptor = dict(formatId="com.project-seam.neural-deployment",
                      schemaVersion=protocol_version, buildId=build_id,
                      platform=str(platform), surface=surface, modulePath=module,
                      manifestPath=manifest_path, manifestSha256=digest)
    if protocol_version == 2:
        descriptor["protocolVersion"] = 2
    encoded = (json.dumps(descriptor, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":")) + "\n").encode("utf-8")
    if len(encoded) > 16 * 1024:
        raise PayloadAssemblyError(("neural deployment descriptor exceeds bounds",))
    return encoded, hashlib.sha256(encoded).hexdigest()


def neural_package_inventory(payload: Path, platform: PayloadPlatform, build_id: str) -> list[dict[str, str]]:
    """Seal explicit absence or verified files; neither state grants release GO."""
    result: list[dict[str, str]] = []
    for surface in surface_matrix(platform):
        if surface.identifier == "installer-verifier":
            continue
        binary = Path(surface.binary_relative_path)
        package, relative_manifest = neural_package_layout(platform, surface)
        manifest_path = package / relative_manifest
        path = payload / manifest_path
        row = {"surface": surface.identifier, "path": manifest_path.as_posix(), "status": "MISSING"}
        if not path.exists() and not path.is_symlink():
            result.append(row)
            continue
        path = require_payload_path(payload, manifest_path.as_posix())
        if not path.is_file() or path.stat().st_size > 256 * 1024:
            raise PayloadAssemblyError(("neural package manifest exceeds its bounds",))
        with path.open("rb") as stream:
            raw = stream.read(256 * 1024 + 1)
        if len(raw) > 256 * 1024:
            raise PayloadAssemblyError(("neural package manifest exceeds its bounds",))
        try:
            value = json.loads(raw)
            if value["buildId"] != build_id or value["module"]["path"] != binary.relative_to(package).as_posix():
                raise ValueError("module or build differs")
            expected, digest = build_neural_package_manifest(
                payload / package, build_id, value["module"]["path"], value["helper"]["path"],
                tuple(entry["path"] for entry in value["dependencies"]),
                protocol_version=value["protocolVersion"])
            if raw != expected:
                raise ValueError("manifest does not match finalized package bytes")
        except (ValueError, TypeError, KeyError, AttributeError, RecursionError) as error:
            raise PayloadAssemblyError((f"neural package manifest is invalid: {surface.identifier}",)) from error
        row.update(status="VERIFIED_FILES", sha256=digest)
        result.append(row)
    return result


def neural_package_layout(
    platform: PayloadPlatform, surface: Surface
) -> tuple[Path, Path]:
    """Return the package root and helper-manifest location for one surface.

    macOS and VST3 bundles keep their resources under ``Contents/Resources``.
    The Windows standalone and CLAP binaries are loose executables, so their
    resources live beside them under ``Resources`` and
    ``ProjectSEAMEditor.resources`` respectively. This is the single owner of
    that layout; staging and inventory both read it.
    """
    package = Path(surface.relative_path)
    if platform == PayloadPlatform.WINDOWS_X64 and surface.identifier in {
        "standalone",
        "clap",
    }:
        package = package.parent
        resources = (
            "Resources"
            if surface.identifier == "standalone"
            else "ProjectSEAMEditor.resources"
        )
        return package, Path(resources) / HELPER_MANIFEST_NAME
    return package, Path("Contents/Resources") / HELPER_MANIFEST_NAME


def build_neural_package_manifest(
    package_root: Path,
    build_id: str,
    module: str,
    helper: str,
    dependencies: tuple[str, ...],
    *, protocol_version: int = 1,
) -> tuple[bytes, str]:
    """Seal an explicit launch version; this does not qualify the helper implementation."""
    if type(protocol_version) is not int or protocol_version not in (1, 2):
        raise PayloadAssemblyError(("neural package protocol version is unsupported",))
    if (not build_id or len(build_id.encode("utf-8")) > 256
            or any(ord(c) < 32 or ord(c) == 127 for c in build_id)
            or len(dependencies) > 64):
        raise PayloadAssemblyError(("neural package build or dependency budget is invalid",))
    root = require_real_directory(package_root, "neural package root")
    seen: set[str] = set()

    def entry(name: str) -> dict[str, str | int]:
        if (not name or len(name.encode("utf-8")) > 4096
                or any(ord(c) < 32 or ord(c) == 127 for c in name)
                or "\\" in name or ":" in name
                or any(part in {"", ".", ".."} for part in name.split("/"))
                or name in seen):
            raise PayloadAssemblyError(("neural package path is invalid or duplicated",))
        seen.add(name)
        path = require_payload_path(root, name)
        if not path.is_file():
            raise PayloadAssemblyError(("neural package entry must be a regular file",))
        before = path.stat()
        if before.st_size > MAXIMUM_FILE_BYTES:
            raise PayloadAssemblyError(("neural package file exceeds its size limit",))
        digest = hashlib.sha256()
        total = 0
        with path.open("rb") as stream:
            while chunk := stream.read(64 * 1024):
                total += len(chunk)
                if total > MAXIMUM_FILE_BYTES:
                    raise PayloadAssemblyError(("neural package file grew beyond its size limit",))
                digest.update(chunk)
        after = path.stat()
        if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns)
                or total != before.st_size):
            raise PayloadAssemblyError(("neural package file changed while hashing",))
        return {"path": name, "sha256": digest.hexdigest(), "maximumBytes": MAXIMUM_FILE_BYTES}

    manifest = {
        "formatId": "com.project-seam.neural-helper-package",
        "schemaVersion": protocol_version,
        "buildId": build_id,
        "protocolVersion": protocol_version,
        "module": entry(module),
        "helper": entry(helper),
        "dependencies": [entry(name) for name in dependencies],
    }
    encoded = (json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":")) + "\n").encode("utf-8")
    if len(encoded) > 256 * 1024:
        raise PayloadAssemblyError(("neural package manifest exceeds its byte limit",))
    return encoded, hashlib.sha256(encoded).hexdigest()
