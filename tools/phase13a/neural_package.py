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
from tools.phase13a.payload_surfaces import PayloadPlatform, surface_matrix

MAXIMUM_FILE_BYTES = 256 * 1024 * 1024


def neural_package_inventory(payload: Path, platform: PayloadPlatform, build_id: str) -> list[dict[str, str]]:
    """Seal explicit absence or verified files; neither state grants release GO."""
    result: list[dict[str, str]] = []
    for surface in surface_matrix(platform):
        if surface.identifier == "installer-verifier":
            continue
        binary = Path(surface.binary_relative_path)
        bundle = Path(surface.relative_path)
        if platform == PayloadPlatform.WINDOWS_X64 and surface.identifier in {"standalone", "clap"}:
            package = bundle.parent
            resources = "Resources" if surface.identifier == "standalone" else "ProjectSEAMEditor.resources"
            relative_manifest = Path(resources) / "neural-helper-package.json"
        else:
            package = bundle
            relative_manifest = Path("Contents/Resources/neural-helper-package.json")
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
                tuple(entry["path"] for entry in value["dependencies"]))
            if raw != expected:
                raise ValueError("manifest does not match finalized package bytes")
        except (ValueError, TypeError, KeyError, AttributeError, RecursionError) as error:
            raise PayloadAssemblyError((f"neural package manifest is invalid: {surface.identifier}",)) from error
        row.update(status="VERIFIED_FILES", sha256=digest)
        result.append(row)
    return result


def build_neural_package_manifest(
    package_root: Path,
    build_id: str,
    module: str,
    helper: str,
    dependencies: tuple[str, ...],
) -> tuple[bytes, str]:
    """Return canonical schema-1 bytes and their digest, without writing files."""
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
        "schemaVersion": 1,
        "buildId": build_id,
        "protocolVersion": 1,
        "module": entry(module),
        "helper": entry(helper),
        "dependencies": [entry(name) for name in dependencies],
    }
    encoded = (json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":")) + "\n").encode("utf-8")
    if len(encoded) > 256 * 1024:
        raise PayloadAssemblyError(("neural package manifest exceeds its byte limit",))
    return encoded, hashlib.sha256(encoded).hexdigest()
