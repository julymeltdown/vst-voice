from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath

from .contracts import JsonObject, JsonValue, is_sha256, sha256_json


def _safe_relative(value: JsonValue) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(
        part not in {"", ".", ".."} for part in path.parts
    )


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _manifest_errors(
    manifest: JsonObject,
    root: Path,
) -> tuple[list[str], dict[str, JsonObject]]:
    errors: list[str] = []
    if manifest.get("schemaVersion") != 1:
        errors.append("manifest schemaVersion must equal 1")
    if manifest.get("recordType") != "project-seam.public-release.archive":
        errors.append("manifest recordType differs")
    if manifest.get("status") != "SEALED":
        errors.append("manifest status must be SEALED")
    if manifest.get("anchored") is not True or manifest.get("immutable") is not True:
        errors.append("manifest must be anchored and immutable")
    stored = manifest.get("manifestSha256")
    unsigned = {key: value for key, value in manifest.items() if key != "manifestSha256"}
    if stored != sha256_json(unsigned):
        errors.append("manifest canonical digest differs")
    entries_value = manifest.get("entries")
    if not isinstance(entries_value, list) or not entries_value:
        return errors + ["manifest entries must be non-empty"], {}
    expected_anchor = sha256_json(
        {
            "archiveId": manifest.get("archiveId"),
            "artifactRootSha256": manifest.get("artifactRootSha256"),
            "entries": entries_value,
        }
    )
    anchor = manifest.get("anchor")
    if not isinstance(anchor, dict):
        errors.append("manifest immutable anchor is required")
    elif (
        anchor.get("kind") != "immutable-object"
        or anchor.get("locator") != f"urn:sha256:{expected_anchor}"
        or anchor.get("sha256") != expected_anchor
    ):
        errors.append("manifest immutable anchor differs")
    entries: dict[str, JsonObject] = {}
    root_resolved = root.resolve()
    for index, entry in enumerate(entries_value):
        label = f"manifest entries[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label} must be an object")
            continue
        path_value = entry.get("path")
        if not _safe_relative(path_value):
            errors.append(f"{label} path is unsafe")
            continue
        assert isinstance(path_value, str)
        if path_value in entries:
            errors.append(f"duplicate archive path: {path_value}")
        entries[path_value] = entry
        if not is_sha256(entry.get("sha256")):
            errors.append(f"{label} digest is invalid")
        size = entry.get("size")
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            errors.append(f"{label} size is invalid")
        path = root / path_value
        if path.is_symlink():
            errors.append(f"{label} restore path is symbolic")
            continue
        try:
            resolved = path.resolve(strict=True)
        except (FileNotFoundError, OSError) as exc:
            errors.append(f"{label} cannot restore file: {exc}")
            continue
        if resolved != root_resolved and root_resolved not in resolved.parents:
            errors.append(f"{label} restore path escapes archive")
        elif not resolved.is_file():
            errors.append(f"{label} restore path is not a file")
        elif is_sha256(entry.get("sha256")) and _sha256_file(resolved) != entry.get("sha256"):
            errors.append(f"{label} restored digest differs")
        elif isinstance(size, int) and resolved.stat().st_size != size:
            errors.append(f"{label} restored size differs")
    return errors, entries


def audit_archive(
    candidate: JsonObject,
    manifest: JsonObject,
    root: Path,
) -> tuple[str, ...]:
    errors, entries = _manifest_errors(manifest, root)
    if manifest.get("candidateLineageId") != candidate.get("candidateLineageId"):
        errors.append("manifest candidate lineage differs")
    chain = candidate.get("rootChain")
    artifact = chain.get("artifactRoot") if isinstance(chain, dict) else None
    evidence_root = chain.get("evidenceRoot") if isinstance(chain, dict) else None
    artifact_sha256 = artifact.get("sha256") if isinstance(artifact, dict) else None
    if manifest.get("artifactRootSha256") != artifact_sha256:
        errors.append("manifest artifact root differs")
    manifest_sha256 = manifest.get("manifestSha256")
    if not isinstance(evidence_root, dict) or evidence_root.get("archiveManifestSha256") != manifest_sha256:
        errors.append("EvidenceRoot archive manifest digest differs")
    archive = candidate.get("archive")
    if not isinstance(archive, dict) or archive.get("manifestSha256") != manifest_sha256:
        errors.append("candidate archive manifest digest differs")
    records = candidate.get("evidence")
    if not isinstance(records, list) or not records:
        return tuple(errors + ["candidate evidence must be non-empty"])
    for index, record in enumerate(records):
        label = f"evidence[{index}]"
        if not isinstance(record, dict):
            errors.append(f"{label} must be an object")
            continue
        raw = record.get("rawArchive")
        if not isinstance(raw, dict) or not isinstance(raw.get("path"), str):
            errors.append(f"{label} raw archive is required")
            continue
        path_value = raw["path"]
        entry = entries.get(path_value)
        if entry is None:
            errors.append(f"{label} raw record is not restored in manifest")
            continue
        if raw.get("sha256") != entry.get("sha256"):
            errors.append(f"{label} raw record digest differs")
        try:
            archived = json.loads((root / path_value).read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            errors.append(f"{label} raw record cannot restore as JSON: {exc}")
            continue
        expected = {key: value for key, value in record.items() if key != "rawArchive"}
        if archived != expected:
            errors.append(f"{label} restored record differs from candidate")
    return tuple(errors)
