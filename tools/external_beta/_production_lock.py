from __future__ import annotations

from typing import Any

from ._production_common import ProductionResult, is_timestamp, sha256_json


def create_beta_lock(
    candidate: dict[str, Any], package: dict[str, Any], canonical_song: dict[str, Any],
    inventory: dict[str, Any], *, generated_at: str,
) -> dict[str, Any]:
    return {
        "schemaVersion": 1,
        "status": "LOCKED",
        "voicebankId": package.get("id"),
        "version": package.get("version"),
        "packageSha256": package.get("contentSha256"),
        "entryManifestSha256": package.get("entryManifestSha256"),
        "candidateSha256": sha256_json(candidate),
        "inventorySha256": inventory.get("inventorySha256"),
        "sourceDerivedTreeSha256": sha256_json(
            {"sourceAssets": candidate.get("sourceAssets", []), "derivedAssets": candidate.get("derivedAssets", [])}
        ),
        "canonicalSong": {
            "projectSha256": canonical_song.get("projectSha256"),
            "mediaSha256": canonical_song.get("mediaSha256"),
        },
        "generatedAt": generated_at,
        "contentChangePolicy": "any package, inventory, candidate, source, derived, project, or media change creates a new lock",
    }


def validate_beta_lock(
    lock: dict[str, Any], candidate: dict[str, Any], package: dict[str, Any],
    inventory: dict[str, Any], canonical_song: dict[str, Any],
) -> ProductionResult:
    errors: list[str] = []
    required = (
        "schemaVersion", "status", "voicebankId", "version", "packageSha256",
        "entryManifestSha256", "candidateSha256", "inventorySha256",
        "sourceDerivedTreeSha256", "canonicalSong", "generatedAt",
    )
    for key in required:
        if key not in lock:
            errors.append(f"lock.{key} is required")
    if lock.get("schemaVersion") != 1 or lock.get("status") != "LOCKED":
        errors.append("lock must be schemaVersion 1 and LOCKED")
    if lock.get("voicebankId") != package.get("id") or lock.get("version") != package.get("version"):
        errors.append("lock bank identity does not match package")
    if lock.get("packageSha256") != package.get("contentSha256"):
        errors.append("lock packageSha256 does not match package")
    if lock.get("entryManifestSha256") != package.get("entryManifestSha256"):
        errors.append("lock entryManifestSha256 does not match package")
    if lock.get("candidateSha256") != sha256_json(candidate):
        errors.append("lock candidateSha256 does not match candidate bytes")
    if lock.get("inventorySha256") != inventory.get("inventorySha256"):
        errors.append("lock inventorySha256 does not match inventory")
    song = lock.get("canonicalSong")
    if (
        not isinstance(song, dict)
        or song.get("projectSha256") != canonical_song.get("projectSha256")
        or song.get("mediaSha256") != canonical_song.get("mediaSha256")
    ):
        errors.append("lock canonicalSong hashes do not match canonical song")
    if not is_timestamp(lock.get("generatedAt")):
        errors.append("lock.generatedAt must be an ISO-8601 timestamp")
    return ProductionResult(not errors, tuple(errors), ())
