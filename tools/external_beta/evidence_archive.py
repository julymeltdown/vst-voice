from __future__ import annotations

import hashlib
import ipaddress
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import urlparse


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
ROLES = {"A3", "A4", "A5", "A6"}
PRIVACY_CLASSES = {"PUBLIC_TECHNICAL", "RESTRICTED_SUPPORT", "PRIVATE_LOCAL"}
FORBIDDEN_PATH_PARTS = {"source", "build", ".git", "build-baseline2", "node_modules"}


@dataclass(frozen=True, slots=True)
class ArchiveValidationResult:
    passed: bool
    errors: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors)}


def _hex(value: Any) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _safe_relative(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in {"", ".", ".."} for part in path.parts)


def _time(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def _immutable_remote_locator(value: Any) -> bool:
    parsed = urlparse(value) if isinstance(value, str) else None
    if parsed is None or parsed.scheme != "https" or not parsed.hostname or parsed.hostname.lower() == "localhost":
        return False
    try:
        return ipaddress.ip_address(parsed.hostname).is_global
    except ValueError:
        return True


def _anchor_sha256(candidate_root_id: str, archive_id: str, locator: str, entries: list[dict[str, Any]], created_at: str, roles: dict[str, str]) -> str:
    return sha256_json({"archiveId": archive_id, "candidateRootId": candidate_root_id, "createdAt": created_at, "entries": entries, "immutable": True, "locator": locator, "recordType": "external-beta-evidence-archive", "roles": roles, "schemaVersion": 1, "status": "SEALED"})


def create_archive_manifest(
    candidate_root_id: str,
    root: Path,
    relative_paths: list[str],
    archive_id: str,
    *,
    anchor_locator: str | None = None,
    producer: str = "A6",
    reviewer: str = "A4",
) -> dict[str, Any]:
    if not candidate_root_id or not archive_id or not _immutable_remote_locator(anchor_locator):
        raise ValueError("candidate root and archive identifiers are required")
    entries: list[dict[str, Any]] = []
    seen: set[str] = set()
    for relative in sorted(relative_paths):
        if not _safe_relative(relative) or relative in seen:
            raise ValueError(f"archive entry path is unsafe or duplicated: {relative}")
        if {part.lower() for part in PurePosixPath(relative).parts} & FORBIDDEN_PATH_PARTS:
            raise ValueError(f"archive entry cannot reference source/build material: {relative}")
        path = root / relative
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"archive entry is not a regular file: {relative}")
        seen.add(relative)
        entries.append({
            "path": relative,
            "kind": "evidence-record",
            "privacyClass": "PUBLIC_TECHNICAL",
            "sha256": _digest(path),
            "size": path.stat().st_size,
        })
    created_at = datetime.now().astimezone().isoformat()
    roles = {"producer": producer, "reviewer": reviewer}
    manifest: dict[str, Any] = {
        "schemaVersion": 1,
        "recordType": "external-beta-evidence-archive",
        "archiveId": archive_id,
        "candidateRootId": candidate_root_id,
        "status": "SEALED",
        "anchored": True,
        "immutable": True,
        "anchor": {"kind": "external-immutable-anchor", "locator": anchor_locator, "sha256": _anchor_sha256(candidate_root_id, archive_id, anchor_locator, entries, created_at, roles)},
        "createdAt": created_at,
        "roles": roles,
        "entries": entries,
    }
    manifest["manifestSha256"] = sha256_json(manifest)
    return manifest


def validate_archive_manifest(manifest: dict[str, Any], root: Path) -> list[str]:
    errors: list[str] = []
    if not isinstance(manifest, dict):
        return ["archive manifest must be an object"]
    if manifest.get("schemaVersion") != 1:
        errors.append("manifest.schemaVersion must be 1")
    if manifest.get("recordType") != "external-beta-evidence-archive":
        errors.append("manifest.recordType is invalid")
    for key in ("archiveId", "candidateRootId", "createdAt"):
        if not isinstance(manifest.get(key), str) or not manifest[key]:
            errors.append(f"manifest.{key} is required")
    if not _time(manifest.get("createdAt")):
        errors.append("manifest.createdAt must be ISO-8601")
    if manifest.get("status") != "SEALED":
        errors.append("manifest.status must be SEALED")
    if manifest.get("anchored") is not True or manifest.get("immutable") is not True:
        errors.append("archive must be externally anchored and immutable")
    anchor = manifest.get("anchor")
    if not isinstance(anchor, dict) or not anchor.get("kind") or not anchor.get("locator") or not _hex(anchor.get("sha256")):
        errors.append("manifest.anchor must include kind, locator, and SHA-256")
    elif not _immutable_remote_locator(anchor.get("locator")) or anchor.get("sha256") != _anchor_sha256(manifest.get("candidateRootId", ""), manifest.get("archiveId", ""), anchor["locator"], manifest.get("entries", []), manifest.get("createdAt", ""), manifest.get("roles", {})):
        errors.append("manifest.anchor must bind a non-local immutable archive commitment")
    roles = manifest.get("roles")
    if not isinstance(roles, dict) or roles.get("producer") not in ROLES or roles.get("reviewer") not in ROLES:
        errors.append("manifest.roles must identify valid producer and reviewer roles")
    elif roles["producer"] == roles["reviewer"] or roles["reviewer"] not in {"A4", "A6"}:
        errors.append("archive reviewer must be independent A4 or A6")
    stored = manifest.get("manifestSha256")
    unsigned = {key: value for key, value in manifest.items() if key != "manifestSha256"}
    if not _hex(stored) or stored != sha256_json(unsigned):
        errors.append("manifestSha256 does not match canonical manifest bytes")
    entries = manifest.get("entries")
    if not isinstance(entries, list) or not entries:
        errors.append("manifest.entries must be non-empty")
        entries = []
    seen: set[str] = set()
    root_resolved = root.resolve()
    for index, entry in enumerate(entries):
        label = f"entries[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label} must be an object")
            continue
        path_value = entry.get("path")
        if not _safe_relative(path_value):
            errors.append(f"{label}.path must be safe and relative")
            continue
        if path_value in seen:
            errors.append(f"duplicate archive entry: {path_value}")
        seen.add(path_value)
        if {part.lower() for part in PurePosixPath(path_value).parts} & FORBIDDEN_PATH_PARTS:
            errors.append(f"{label}.path cannot reference source/build material")
        if entry.get("privacyClass") not in PRIVACY_CLASSES:
            errors.append(f"{label}.privacyClass is invalid")
        if not _hex(entry.get("sha256")) or not isinstance(entry.get("size"), int) or entry["size"] < 1:
            errors.append(f"{label} digest and positive size are required")
        path = root / path_value
        if path.is_symlink():
            errors.append(f"{label}.path is symbolic")
            continue
        try:
            resolved = path.resolve(strict=True)
            if root_resolved != resolved and root_resolved not in resolved.parents:
                errors.append(f"{label}.path escapes archive root")
            elif not resolved.is_file():
                errors.append(f"{label}.path is not a regular file")
            elif _hex(entry.get("sha256")) and _digest(resolved) != entry["sha256"].lower():
                errors.append(f"{label}.sha256 does not match restored bytes")
            elif isinstance(entry.get("size"), int) and resolved.stat().st_size != entry["size"]:
                errors.append(f"{label}.size does not match restored bytes")
        except (FileNotFoundError, OSError) as exc:
            errors.append(f"{label}.path cannot be restored: {exc}")
    return errors


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value
