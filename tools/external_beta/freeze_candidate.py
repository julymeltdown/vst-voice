from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


HEX40 = re.compile(r"^[0-9a-fA-F]{40}$")
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")


@dataclass(frozen=True, slots=True)
class FreezeCandidateInputError(ValueError):
    message: str

    def __str__(self) -> str:
        return self.message


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def _require_digest(value: Any, label: str) -> None:
    if not isinstance(value, str) or HEX64.fullmatch(value) is None:
        raise FreezeCandidateInputError(
            f"{label} must be a 64-character hexadecimal digest"
        )


def _require_roles(authorization: dict[str, Any]) -> None:
    roles = authorization.get("approvals")
    if not isinstance(roles, list):
        raise FreezeCandidateInputError("approvals must be a list")
    approved = {item.get("role") for item in roles if isinstance(item, dict) and item.get("status") == "APPROVED"}
    required = {"A3", "A4", "A5", "A6"}
    if not required.issubset(approved):
        raise FreezeCandidateInputError(
            "authorization requires approved A3, A4, A5, and A6 roles"
        )


def _manifest_key(manifest: dict[str, Any]) -> str:
    value = {key: manifest[key] for key in sorted(manifest) if key not in {"createdAt", "workspace", "outputPath"}}
    return canonical_json(value)


def validate_authorization(authorization: dict[str, Any], build_manifests: list[dict[str, Any]]) -> None:
    if authorization.get("schemaVersion") != 1:
        raise FreezeCandidateInputError("authorization schemaVersion must equal 1")
    if authorization.get("status") != "GO":
        raise FreezeCandidateInputError("release authorization must be GO")
    source_commit = authorization.get("sourceCommit")
    if not isinstance(source_commit, str) or HEX40.fullmatch(source_commit) is None:
        raise FreezeCandidateInputError(
            "authorization.sourceCommit must be a 40-character commit"
        )
    for key in (
        "bankSha256",
        "trustPolicySha256",
        "documentationSha256",
        "sbomSha256",
        "predecessorSha256",
        "archiveSha256",
        "acceptanceContractSha256",
    ):
        _require_digest(authorization.get(key), f"authorization.{key}")
    if authorization.get("archiveRestored") is not True:
        raise FreezeCandidateInputError(
            "authorization.archiveRestored must be true"
        )
    if authorization.get("signingCredentialsExcluded") is not True:
        raise FreezeCandidateInputError(
            "signing credentials must be excluded from the freeze environment"
        )
    _require_roles(authorization)
    if len(build_manifests) < 2:
        raise FreezeCandidateInputError(
            "at least two independent unsigned build manifests are required"
        )
    platforms: dict[str, list[dict[str, Any]]] = {}
    for index, manifest in enumerate(build_manifests):
        if not isinstance(manifest, dict):
            raise FreezeCandidateInputError(
                f"buildManifests[{index}] must be an object"
            )
        platform = manifest.get("platform")
        if platform not in {"macos", "windows"}:
            raise FreezeCandidateInputError(
                f"buildManifests[{index}].platform is unsupported"
            )
        if manifest.get("status") != "PASS" or manifest.get("signed") is not False:
            raise FreezeCandidateInputError(
                f"buildManifests[{index}] must be an unsigned PASS"
            )
        _require_digest(manifest.get("manifestSha256"), f"buildManifests[{index}].manifestSha256")
        if manifest.get("sourceCommit") != source_commit:
            raise FreezeCandidateInputError(
                f"buildManifests[{index}] sourceCommit differs from authorization"
            )
        platforms.setdefault(platform, []).append(manifest)
    for platform in ("macos", "windows"):
        if len(platforms.get(platform, [])) < 2:
            raise FreezeCandidateInputError(
                f"two independent unsigned {platform} manifests are required"
            )
        if len({_manifest_key(manifest) for manifest in platforms[platform]}) != 1:
            raise FreezeCandidateInputError(
                f"independent {platform} unsigned manifests differ"
            )


def issue_candidate_id(authorization: dict[str, Any], issued_ids: set[str] | None = None) -> str:
    validate_authorization(authorization, authorization.get("buildManifests", []))
    seed = authorization.get("candidateSeed")
    if not isinstance(seed, str) or not seed:
        raise FreezeCandidateInputError("candidateSeed is required")
    candidate_id = "seam-beta-" + hashlib.sha256(
        ("candidate-v1:" + seed + ":" + authorization["sourceCommit"] + ":" + authorization["archiveSha256"]).encode("utf-8")
    ).hexdigest()[:24]
    if issued_ids is not None and candidate_id in issued_ids:
        raise FreezeCandidateInputError("candidate ID has already been issued")
    return candidate_id


def freeze_candidate(
    authorization: dict[str, Any],
    build_manifests: list[dict[str, Any]],
    issued_ids: set[str] | None = None,
) -> dict[str, Any]:
    authorization = dict(authorization)
    authorization["buildManifests"] = build_manifests
    validate_authorization(authorization, build_manifests)
    candidate_id = issue_candidate_id(authorization, issued_ids)
    platform_manifests = {
        platform: next(manifest for manifest in build_manifests if manifest.get("platform") == platform)
        for platform in ("macos", "windows")
    }
    record = {
        "schemaVersion": 1,
        "candidateId": candidate_id,
        "status": "FROZEN_UNSIGNED",
        "sourceCommit": authorization["sourceCommit"],
        "releaseIdentity": authorization.get("releaseIdentity", {}),
        "authorizationSha256": sha256_json(authorization),
        "bankSha256": authorization["bankSha256"],
        "trustPolicySha256": authorization["trustPolicySha256"],
        "documentationSha256": authorization["documentationSha256"],
        "sbomSha256": authorization["sbomSha256"],
        "predecessorSha256": authorization["predecessorSha256"],
        "archiveSha256": authorization["archiveSha256"],
        "acceptanceContractSha256": authorization["acceptanceContractSha256"],
        "platformManifests": platform_manifests,
        "signingCredentialsExcluded": True,
    }
    record["freezeSha256"] = sha256_json(record)
    return record


def write_freeze(record: dict[str, Any], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(canonical_json(record) + "\n", encoding="utf-8")
