#!/usr/bin/env python3
from __future__ import annotations

import datetime as _datetime
import hashlib
import json
import os
import re
import stat
import zipfile
from pathlib import Path
from typing import Any, Iterable, Mapping


SCHEMA_VERSION = 1
MAX_EVENTS = 256
MAX_ATTACHMENTS = 8
MAX_ARCHIVE_BYTES = 8 * 1024 * 1024
MAX_ENTRY_BYTES = 1024 * 1024
DEFAULT_TIMESTAMP = "1970-01-01T00:00:00Z"
SAFE_CODE = re.compile(r"^[A-Z][A-Z0-9_.-]{1,63}$")
SAFE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
SAFE_OS = re.compile(r"^[A-Za-z0-9._ -]{1,64}$")
SAFE_STACK = re.compile(r"^[A-Za-z0-9_.$<>: -]{1,160}$")
FORBIDDEN_KEYS = {
    "raw", "rawLog", "rawLogs", "stderr", "stdout", "errorContext", "dump", "minidump", "coreDump",
    "path", "fullPath", "projectPath", "mediaPath", "bankPath", "lyrics", "audio", "pcm", "samples",
    "environment", "env", "username", "userId", "deviceId", "hostString", "hostName", "secret", "token",
}
EXPORT_FIELD_TYPES = {
    "buildId": str,
    "sourceCommit": str,
    "artifactId": str,
    "artifactSha256": str,
    "bankId": str,
    "bankVersion": str,
    "bankContentHash": str,
    "osFamily": str,
    "osMajor": int,
    "hostFamily": str,
    "hostMajor": int,
    "deviceFamily": str,
    "sampleRate": int,
    "bufferFrames": int,
    "channels": int,
    "diagnosticCount": int,
    "xrunCount": int,
    "renderCount": int,
    "recoveryState": str,
    "manifestVersion": int,
    "sanitizedStackSymbols": list,
}


def _now() -> str:
    return _datetime.datetime.now(_datetime.timezone.utc).isoformat().replace("+00:00", "Z")


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _safe_string(value: Any, label: str, pattern: re.Pattern[str] = SAFE_ID) -> str:
    if not isinstance(value, str) or not pattern.fullmatch(value):
        raise ValueError(f"{label} contains an unsafe or invalid value")
    return value


def _safe_number(value: Any, label: str, maximum: int = 1_000_000_000) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0 or value > maximum:
        raise ValueError(f"{label} must be a bounded non-negative integer")
    return value


def _reject_forbidden(value: Any, path: str = "event") -> None:
    if isinstance(value, Mapping):
        for key, child in value.items():
            if str(key) in FORBIDDEN_KEYS or str(key).lower() in {item.lower() for item in FORBIDDEN_KEYS}:
                raise ValueError(f"{path}.{key} is not export-safe")
            _reject_forbidden(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _reject_forbidden(child, f"{path}[{index}]")


def sanitize_event(event: Mapping[str, Any]) -> dict[str, Any]:
    if not isinstance(event, Mapping):
        raise ValueError("diagnostic event must be an object")
    _reject_forbidden(event)
    code = _safe_string(event.get("code"), "code", SAFE_CODE)
    severity = event.get("severity")
    if severity not in {"info", "warning", "error", "fatal"}:
        raise ValueError("severity must be info, warning, error, or fatal")
    fields = event.get("fields", {})
    if not isinstance(fields, Mapping):
        raise ValueError("fields must be an object")
    unknown = set(fields) - set(EXPORT_FIELD_TYPES)
    if unknown:
        raise ValueError("fields contain non-export-safe keys: " + ", ".join(sorted(map(str, unknown))))
    safe_fields: dict[str, Any] = {}
    for key, expected_type in EXPORT_FIELD_TYPES.items():
        if key not in fields:
            continue
        value = fields[key]
        if expected_type is int:
            safe_fields[key] = _safe_number(value, f"fields.{key}")
        elif expected_type is list:
            if not isinstance(value, list) or len(value) > 64:
                raise ValueError("fields.sanitizedStackSymbols must be a bounded array")
            safe_fields[key] = [_safe_string(item, "sanitizedStackSymbols", SAFE_STACK) for item in value]
        else:
            pattern = SAFE_OS if key in {"osFamily", "hostFamily", "deviceFamily"} else SAFE_ID
            safe_fields[key] = _safe_string(value, f"fields.{key}", pattern)
        if key.endswith("Sha256") or key.endswith("Hash") or key == "sourceCommit":
            if not re.fullmatch(r"[0-9a-fA-F]{16,128}", safe_fields[key]):
                raise ValueError(f"fields.{key} must be a hexadecimal identity")
    occurred = event.get("occurredAt", DEFAULT_TIMESTAMP)
    if not isinstance(occurred, str) or not occurred:
        raise ValueError("occurredAt must be a timestamp string")
    try:
        parsed_occurred = _datetime.datetime.fromisoformat(occurred.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError("occurredAt must be an ISO-8601 timestamp") from exc
    if parsed_occurred.tzinfo is None:
        raise ValueError("occurredAt must include a timezone")
    return {"code": code, "severity": severity, "occurredAt": occurred, "fields": safe_fields}


def sanitize_events(events: Iterable[Mapping[str, Any]]) -> list[dict[str, Any]]:
    values = list(events)
    if len(values) > MAX_EVENTS:
        raise ValueError(f"at most {MAX_EVENTS} diagnostic events are allowed")
    return [sanitize_event(event) for event in values]


def _safe_archive_name(value: str) -> str:
    name = Path(value).name
    if name != value or name in {"", ".", ".."} or "\x00" in name:
        raise ValueError("attachment name must be a portable basename")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", name):
        raise ValueError("attachment name contains unsupported characters")
    return name


def _zip_bytes(events: list[dict[str, Any]], attachments: list[tuple[str, bytes]], metadata: Mapping[str, Any]) -> bytes:
    diagnostics = json.dumps({"schemaVersion": SCHEMA_VERSION, "events": events}, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    entries: list[tuple[str, bytes]] = [("diagnostics.json", diagnostics)]
    for name, value in attachments:
        entries.append((f"attachments/{name}", value))
    if any(len(value) > MAX_ENTRY_BYTES for _, value in entries):
        raise ValueError("support bundle entry exceeds the per-entry limit")
    manifest_entries = [{"path": name, "size": len(value), "sha256": _sha256(value)} for name, value in entries]
    manifest = {
        "schemaVersion": SCHEMA_VERSION,
        "purpose": "export-safe-support-bundle",
        "privacyClass": "ExportSafe",
        "createdAt": metadata.get("createdAt", DEFAULT_TIMESTAMP),
        "consent": bool(metadata.get("consent", False)),
        "entries": manifest_entries,
    }
    manifest_bytes = json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    entries.insert(0, ("manifest.json", manifest_bytes))
    output = bytearray()
    import io
    with io.BytesIO() as buffer:
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_STORED) as archive:
            for name, value in entries:
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = (stat.S_IFREG | stat.S_IRUSR | stat.S_IWUSR) << 16
                archive.writestr(info, value)
        output.extend(buffer.getvalue())
    if len(output) > MAX_ARCHIVE_BYTES:
        raise ValueError("support bundle exceeds the archive limit")
    return bytes(output)


def build_export_bundle(events: Iterable[Mapping[str, Any]], *, attachments: Iterable[Path] = (), consent: bool = False, metadata: Mapping[str, Any] | None = None) -> tuple[bytes, dict[str, Any]]:
    safe_events = sanitize_events(events)
    attachment_values: list[tuple[str, bytes]] = []
    attachment_paths = list(attachments)
    if attachment_paths and not consent:
        raise ValueError("optional attachments require explicit per-bundle consent")
    if len(attachment_paths) > MAX_ATTACHMENTS:
        raise ValueError(f"at most {MAX_ATTACHMENTS} attachments are allowed")
    for path in attachment_paths:
        candidate = Path(path)
        if candidate.is_symlink() or not candidate.is_file():
            raise ValueError("attachments must be regular files and cannot be symbolic links")
        if candidate.stat().st_size > MAX_ENTRY_BYTES:
            raise ValueError("attachment exceeds the per-entry limit")
        attachment_values.append((_safe_archive_name(candidate.name), candidate.read_bytes()))
    archive = _zip_bytes(safe_events, attachment_values, {**(metadata or {}), "consent": consent})
    preview = {"schemaVersion": SCHEMA_VERSION, "privacyClass": "ExportSafe", "archiveBytes": len(archive), "archiveSha256": _sha256(archive), "entryNames": ["manifest.json", "diagnostics.json"] + [f"attachments/{name}" for name, _ in attachment_values], "consent": consent}
    return archive, preview


def preview_export_bundle(events: Iterable[Mapping[str, Any]], *, attachments: Iterable[Path] = (), consent: bool = False, metadata: Mapping[str, Any] | None = None) -> dict[str, Any]:
    _, preview = build_export_bundle(events, attachments=attachments, consent=consent, metadata=metadata)
    return preview


def write_export_bundle(destination: Path, events: Iterable[Mapping[str, Any]], *, attachments: Iterable[Path] = (), consent: bool = False, metadata: Mapping[str, Any] | None = None) -> dict[str, Any]:
    archive, preview = build_export_bundle(events, attachments=attachments, consent=consent, metadata=metadata)
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise ValueError("support bundle destination already exists or is a symbolic link")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".partial")
    if temporary.exists() or temporary.is_symlink():
        raise ValueError("support bundle temporary destination already exists")
    temporary.write_bytes(archive)
    temporary.chmod(stat.S_IRUSR | stat.S_IWUSR)
    os.replace(temporary, destination)
    return preview


def write_private_report(root: Path, report: Mapping[str, Any]) -> Path:
    destination_root = Path(root).expanduser().resolve()
    if destination_root.exists() and destination_root.is_symlink():
        raise ValueError("private report root cannot be a symbolic link")
    destination_root.mkdir(parents=True, exist_ok=True)
    destination_root.chmod(stat.S_IRWXU)
    report_id = _safe_string(report.get("reportId"), "reportId")
    destination = destination_root / f"{report_id}.json"
    if destination.exists() or destination.is_symlink():
        raise ValueError("report already exists")
    payload = json.dumps(dict(report), ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if len(payload) > MAX_ENTRY_BYTES:
        raise ValueError("private report exceeds the per-report limit")
    destination.write_bytes(payload)
    destination.chmod(stat.S_IRUSR | stat.S_IWUSR)
    return destination


def delete_owned_report(path: Path, root: Path) -> None:
    root_resolved = Path(root).expanduser().resolve()
    candidate = Path(path).expanduser().resolve()
    if not _is_relative_to(candidate, root_resolved) or candidate.parent != root_resolved or candidate.is_symlink():
        raise ValueError("report is outside the owned private store")
    if candidate.exists():
        candidate.unlink()


def write_crash_marker(root: Path, code: str) -> Path:
    safe_code = _safe_string(code, "code", SAFE_CODE)
    destination_root = Path(root).expanduser().resolve()
    destination_root.mkdir(parents=True, exist_ok=True)
    destination = destination_root / "crash-marker.json"
    if destination.is_symlink():
        raise ValueError("crash marker cannot be a symbolic link")
    payload = {"schemaVersion": SCHEMA_VERSION, "purpose": "local-crash-marker", "code": safe_code, "createdAt": _now()}
    destination.write_bytes(json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    destination.chmod(stat.S_IRUSR | stat.S_IWUSR)
    return destination


__all__ = ["SCHEMA_VERSION", "build_export_bundle", "delete_owned_report", "preview_export_bundle", "sanitize_event", "sanitize_events", "write_crash_marker", "write_export_bundle", "write_private_report"]
