from __future__ import annotations

import hashlib
import json
from pathlib import Path
import zipfile

from common import safe_relative_path, sha256_file

_FIXED_TIME = (1980, 1, 1, 0, 0, 0)
_MAX_FILE_BYTES = 256 * 1024 * 1024
_MAX_TOTAL_BYTES = 1024 * 1024 * 1024


def _entry(archive: zipfile.ZipFile, name: str, payload: bytes) -> None:
    if not safe_relative_path(name):
        raise ValueError(f"unsafe bundle path: {name}")
    info = zipfile.ZipInfo(name, _FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, payload)


def _collect(components: dict[str, Path]) -> tuple[dict[str, bytes], list[dict]]:
    payloads: dict[str, bytes] = {}
    entries: list[dict] = []
    total = 0
    for prefix, root_value in sorted(components.items()):
        if not safe_relative_path(prefix):
            raise ValueError(f"unsafe component prefix: {prefix}")
        root = Path(root_value)
        if root.is_symlink() or not root.is_dir():
            raise ValueError(f"component root must be a regular directory: {root}")
        resolved_root = root.resolve(strict=True)
        for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
            if path.is_symlink():
                raise ValueError(f"component contains a symbolic link: {path}")
            if not path.is_file():
                continue
            resolved = path.resolve(strict=True)
            if resolved_root not in resolved.parents:
                raise ValueError(f"component file escapes root: {path}")
            relative = path.relative_to(root).as_posix()
            name = f"{prefix}/{relative}"
            if not safe_relative_path(name) or any(part.startswith(".") for part in Path(name).parts):
                raise ValueError(f"unsafe component file path: {name}")
            size = path.stat().st_size
            if size > _MAX_FILE_BYTES:
                raise ValueError(f"component file exceeds {_MAX_FILE_BYTES} bytes: {name}")
            total += size
            if total > _MAX_TOTAL_BYTES:
                raise ValueError("bundle payload exceeds 1 GiB")
            payload = path.read_bytes()
            digest = hashlib.sha256(payload).hexdigest()
            payloads[name] = payload
            entries.append({"path": name, "size": len(payload), "sha256": digest})
    if not entries:
        raise ValueError("bundle must contain at least one file")
    return payloads, entries


def _build(output: Path, components: dict[str, Path], version: str, release_eligible: bool, blocked: list[str]) -> dict:
    payloads, entries = _collect(components)
    content_payload = {
        "schemaVersion": 1,
        "productVersion": version,
        "releaseEligible": release_eligible,
        "developmentOnly": not release_eligible,
        "blockedTargets": sorted(set(blocked)),
        "entries": entries,
    }
    canonical = json.dumps(content_payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    content_digest = hashlib.sha256(canonical).hexdigest()
    manifest = {**content_payload, "contentSha256": content_digest}
    manifest_bytes = (json.dumps(manifest, sort_keys=True, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w") as archive:
        for name in sorted(payloads):
            _entry(archive, name, payloads[name])
        manifest_name = "ProjectSEAM/content-manifest.json" if any(name.startswith("ProjectSEAM/") for name in payloads) else "content-manifest.json"
        _entry(archive, manifest_name, manifest_bytes)
        if not release_eligible:
            marker_name = "ProjectSEAM/DEVELOPMENT_ONLY.txt" if manifest_name.startswith("ProjectSEAM/") else "DEVELOPMENT_ONLY.txt"
            _entry(archive, marker_name, b"This content bundle is not approved for public release.\n")
    digest = sha256_file(output)
    return {**manifest, "path": str(output), "packageSha256": digest, "sha256": digest}


def create_development_bundle(*args, version: str | None = None, **kwargs) -> dict:
    """Create a deterministic development bundle.

    Supported call forms:
      create_development_bundle(output, components, version)
      create_development_bundle(voice_root, character_root, output, version=...)
    """
    if len(args) == 3 and isinstance(args[1], dict):
        output, components, positional_version = args
        resolved_version = version or str(positional_version)
    elif len(args) == 3:
        voice_root, character_root, output = args
        components = {
            "ProjectSEAM/voicebank-demo": Path(voice_root),
            "ProjectSEAM/character-development": Path(character_root),
        }
        resolved_version = version
    else:
        output = kwargs.get("output")
        components = kwargs.get("components")
        resolved_version = version or kwargs.get("product_version")
    if output is None or not isinstance(components, dict) or not resolved_version:
        raise TypeError("create_development_bundle requires output, component roots, and version")
    return _build(Path(output), components, str(resolved_version), False, ["official-voicebank-01", "character-01-release"])


def create_official_bundle(output: Path, components: dict[str, Path], version: str, release_result: dict) -> dict:
    if release_result.get("passed") is not True:
        raise ValueError("official content bundle requires a passing G5 release gate")
    return _build(output, components, version, True, [])
