from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from tools.phase13a.distribution_manifest import tree_sha256
from tools.phase13a.payload_identity import validate_surface_identities
from tools.phase13a.payload_paths import require_real_directory
from tools.phase13a.payload_trust import validate_release_trust
from tools.phase13a.release_payload import (
    MANIFEST_NAME,
    PayloadAssemblyError,
    PayloadPlatform,
    ReleaseIdentity,
    _entry,
    _read_identity,
    _read_json,
    _resource_inventory_path,
    _surface_matrix,
    _validate_resource_inventory,
    validate_payload_shape,
)


@dataclass(frozen=True, slots=True)
class VerifiedPayloadManifest:
    platform: PayloadPlatform
    payload_root: Path
    identity: ReleaseIdentity
    manifest_path: Path
    surface_paths: tuple[Path, ...]


def _verify_entry(
    payload: Path,
    expected_identifier: str,
    expected_path: str,
    value: object,
) -> Path:
    if not isinstance(value, dict):
        raise PayloadAssemblyError(
            (f"{expected_identifier}: manifest entry must be an object",)
        )
    actual = _entry(payload, expected_identifier, expected_path)
    if value != actual:
        raise PayloadAssemblyError(
            (f"{expected_identifier}: payload bytes differ from sealed manifest",)
        )
    return payload / expected_path


def verify_release_payload_manifest(
    payload_root: Path,
    expected_platform: PayloadPlatform,
) -> VerifiedPayloadManifest:
    payload = require_real_directory(payload_root, "payload root")
    validate_payload_shape(payload, expected_platform)
    manifest_path = payload / MANIFEST_NAME
    value = _read_json(manifest_path)
    if (
        value.get("schemaVersion") != 1
        or value.get("purpose") != "project-seam-release-payload"
        or value.get("platform") != expected_platform
    ):
        raise PayloadAssemblyError(("release payload manifest identity is invalid",))
    identity = _read_identity(payload / "RELEASE_IDENTITY.json")
    if value.get("releaseIdentity") != identity.to_json():
        raise PayloadAssemblyError(("release identity differs from sealed manifest",))
    if issues := validate_surface_identities(
        payload, expected_platform, identity.to_json()
    ):
        raise PayloadAssemblyError(issues)
    trust_test_only, trust_issues = validate_release_trust(payload)
    if trust_issues or value.get("developmentTrustOnly") is not trust_test_only:
        raise PayloadAssemblyError(trust_issues or ("sealed trust mode differs",))
    inventory_path = _resource_inventory_path(expected_platform)
    _validate_resource_inventory(payload / inventory_path)
    bank_sidecar = value.get("bankSidecar")
    if (
        value.get("sourceClean") is not True
        or value.get("releaseEligible") is not False
        or not isinstance(bank_sidecar, dict)
        or bank_sidecar.get("distribution") != "external-per-user"
        or bank_sidecar.get("bundledVoicebanks") != []
        or bank_sidecar.get("resourceInventory") != inventory_path
        or bank_sidecar.get("resourceInventorySha256")
        != tree_sha256(payload / inventory_path)
    ):
        raise PayloadAssemblyError(
            ("sealed release eligibility or bank handoff is invalid",)
        )
    closure = _read_json(payload / "release-dependency-closure.json")
    if closure.get("status") != "PASS" or closure.get("platform") != expected_platform:
        raise PayloadAssemblyError(
            ("sealed dependency closure is not a matching PASS",)
        )
    if value.get("identitySha256") != tree_sha256(payload / "RELEASE_IDENTITY.json"):
        raise PayloadAssemblyError(("release identity bytes changed after assembly",))

    surfaces = _surface_matrix(expected_platform)
    manifest_surfaces = value.get("surfaces")
    if not isinstance(manifest_surfaces, list) or len(manifest_surfaces) != len(
        surfaces
    ):
        raise PayloadAssemblyError(("sealed surface matrix is incomplete",))
    surface_paths = tuple(
        _verify_entry(
            payload,
            surface.identifier,
            surface.relative_path,
            manifest_surfaces[index],
        )
        for index, surface in enumerate(surfaces)
    )

    for field, identifier in (
        ("ownership", "installer-ownership"),
        ("dependencyClosure", "dependency-closure"),
        ("sbom", "sbom"),
    ):
        entry = value.get(field)
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise PayloadAssemblyError((f"{identifier}: sealed entry is missing",))
        _verify_entry(payload, identifier, entry["path"], entry)
    for field, identifier in (
        ("trustInputs", "release-trust-roots"),
        ("documents", "document"),
        ("notices", "notice"),
    ):
        entries = value.get(field)
        if not isinstance(entries, list) or not entries:
            raise PayloadAssemblyError((f"{identifier}: sealed entries are missing",))
        if field == "trustInputs" and len(entries) != 2:
            raise PayloadAssemblyError(("release trust inputs are incomplete",))
        for index, entry in enumerate(entries):
            if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
                raise PayloadAssemblyError((f"{identifier}: sealed entry is invalid",))
            expected_identifier = (
                ("release-trust-roots", "update-root-public-key")[index]
                if field == "trustInputs"
                else f"{identifier}-{index + 1}"
            )
            _verify_entry(payload, expected_identifier, entry["path"], entry)

    if value.get("payloadTreeSha256") != tree_sha256(payload, {MANIFEST_NAME}):
        raise PayloadAssemblyError(("payload tree differs from sealed manifest",))
    return VerifiedPayloadManifest(
        platform=expected_platform,
        payload_root=payload,
        identity=identity,
        manifest_path=manifest_path,
        surface_paths=surface_paths,
    )
