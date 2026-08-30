from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Final, assert_never

from tools.phase13a.distribution_manifest import tree_sha256
from tools.phase13a.payload_identity import validate_surface_identities
from tools.phase13a.payload_paths import (
    PayloadAssemblyError,
    payload_entry as _entry,
    require_real_directory,
    require_payload_path,
)
from tools.phase13a.payload_surfaces import (
    PayloadPlatform,
    surface_matrix as _surface_matrix,
)
from tools.phase13a.payload_trust import validate_release_trust
from tools.phase13a.release_identity import read_project_version


MANIFEST_NAME: Final = "release-payload-manifest.json"


@dataclass(frozen=True, slots=True)
class PayloadManifest:
    platform: PayloadPlatform
    path: Path
    surface_ids: tuple[str, ...]
    payload_sha256: str


@dataclass(frozen=True, slots=True)
class ReleaseIdentity:
    version: str
    build_id: str
    source_commit: str
    build_epoch: int

    def to_json(self) -> dict[str, str | int]:
        return {
            "product": "Project SEAM",
            "version": self.version,
            "buildId": self.build_id,
            "sourceCommit": self.source_commit,
            "buildEpoch": self.build_epoch,
        }


def resolve_payload_platform(
    host_system: str, host_machine: str, include_auv2: bool
) -> tuple[PayloadPlatform | None, tuple[str, ...]]:
    machine = host_machine.casefold()
    errors: list[str] = []
    if include_auv2 and host_system != "Darwin":
        errors.append("AUv2 target build requires macOS")
    if include_auv2 and host_system == "Darwin" and machine not in {"arm64", "aarch64"}:
        errors.append("macOS release payload requires Apple Silicon")
    if host_system == "Windows" and machine not in {"amd64", "x64", "x86_64"}:
        errors.append("Windows release payload requires x64")
    if host_system == "Windows" and not errors:
        return PayloadPlatform.WINDOWS_X64, ()
    if host_system == "Darwin" and include_auv2 and not errors:
        return PayloadPlatform.MACOS_ARM64, ()
    return None, tuple(errors)


def _read_json(path: Path):
    if path.is_symlink() or not path.is_file():
        raise PayloadAssemblyError((f"required file is missing or linked: {path}",))
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PayloadAssemblyError((f"invalid JSON at {path}: {error}",)) from error
    if not isinstance(value, dict):
        raise PayloadAssemblyError((f"JSON root must be an object: {path}",))
    return value


def _read_identity(path: Path) -> ReleaseIdentity:
    value = _read_json(path)
    product = value.get("product")
    version = value.get("version")
    build_id = value.get("buildId")
    source_commit = value.get("sourceCommit")
    build_epoch = value.get("buildEpoch")
    if product != "Project SEAM":
        raise PayloadAssemblyError(("release identity product is invalid",))
    if (
        not isinstance(version, str)
        or not isinstance(build_id, str)
        or not isinstance(source_commit, str)
        or not isinstance(build_epoch, int)
    ):
        raise PayloadAssemblyError(("release identity fields are invalid",))
    return ReleaseIdentity(version, build_id, source_commit, build_epoch)


def _git(source_root: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(source_root), *arguments],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise PayloadAssemblyError((f"source Git query failed: {' '.join(arguments)}",))
    return completed.stdout.strip()


def _validate_source(source_root: Path, identity: ReleaseIdentity) -> None:
    if _git(source_root, "status", "--porcelain", "--untracked-files=all"):
        raise PayloadAssemblyError(("source checkout is dirty",))
    head = _git(source_root, "rev-parse", "HEAD")
    if identity.source_commit.lower() != head.lower():
        raise PayloadAssemblyError(
            ("release identity sourceCommit differs from Git HEAD",)
        )
    if identity.version != read_project_version(source_root):
        raise PayloadAssemblyError(("release identity version differs from source",))


def _resource_inventory_path(platform: PayloadPlatform) -> str:
    match platform:
        case PayloadPlatform.MACOS_ARM64:
            return "Standalone/Project SEAM.app/Contents/Resources/release-resource-inventory.json"
        case PayloadPlatform.WINDOWS_X64:
            return "Standalone/Resources/release-resource-inventory.json"
        case unreachable:
            assert_never(unreachable)


def _validate_resource_inventory(path: Path) -> None:
    value = _read_json(path)
    character = value.get("characterPackage")
    if (
        value.get("bundledVoicebanks") != []
        or value.get("voicebankHandoff") != "per-user-installed-catalog"
        or not isinstance(character, dict)
        or character.get("required") is not False
        or character.get("bundled") is not False
    ):
        raise PayloadAssemblyError(
            ("release resource inventory is not external-bank neutral",)
        )


def validate_payload_shape(payload: Path, platform: PayloadPlatform) -> None:
    if payload.is_symlink() or not payload.is_dir():
        raise PayloadAssemblyError(("payload root must be a real directory",))
    match platform:
        case PayloadPlatform.MACOS_ARM64:
            expected_children = {
                "Standalone": {"Project SEAM.app"},
                "CLAP": {"ProjectSEAMEditor.clap"},
                "VST3": {"ProjectSEAMEditor.vst3"},
                "AU": {"ProjectSEAMEditor.component"},
                "Tools": {"seam_installer_verifier"},
            }
        case PayloadPlatform.WINDOWS_X64:
            expected_children = {
                "Standalone": {"seam_editor_native.exe", "Resources"},
                "CLAP": {
                    "ProjectSEAMEditor.clap",
                    "ProjectSEAMEditor.resources",
                },
                "VST3": {"ProjectSEAMEditor.vst3"},
                "Tools": {"seam_installer_verifier.exe"},
            }
        case unreachable:
            assert_never(unreachable)
    expected_children.update(
        {
            "Trust": {
                "release-trust-roots.json",
                "update-root-public-key.json",
            },
            "Ownership": {"installer-ownership.json"},
        }
    )
    allowed_roots = {
        "Documentation",
        MANIFEST_NAME,
        "Notices",
        "Ownership",
        "RELEASE_IDENTITY.json",
        "SBOM.spdx.json",
        "THIRD_PARTY_NOTICES.md",
        "Trust",
        "release-dependency-closure.json",
        *expected_children,
    }
    issues: list[str] = []
    unexpected_roots = sorted(
        item.name for item in payload.iterdir() if item.name not in allowed_roots
    )
    if unexpected_roots:
        issues.append(
            f"payload contains undeclared top-level entries: {', '.join(unexpected_roots)}"
        )
    for root_name, expected in expected_children.items():
        root = payload / root_name
        if root.is_symlink() or not root.is_dir():
            issues.append(
                f"{root_name.casefold()} surface directory is missing or linked"
            )
            continue
        actual = {item.name for item in root.iterdir()}
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        if missing:
            issues.append(
                f"{root_name.casefold()} is missing declared children: {', '.join(missing)}"
            )
        if unexpected:
            issues.append(
                f"{root_name.casefold()} contains undeclared children: {', '.join(unexpected)}"
            )
    if issues:
        raise PayloadAssemblyError(tuple(issues))


def assemble_release_payload(
    payload_root: Path,
    source_root: Path,
    platform: PayloadPlatform,
) -> PayloadManifest:
    payload = require_real_directory(payload_root, "payload root")
    source = require_real_directory(source_root, "source root")
    validate_payload_shape(payload, platform)
    identity = _read_identity(payload / "RELEASE_IDENTITY.json")
    _validate_source(source, identity)
    if identity_issues := validate_surface_identities(
        payload, platform, identity.to_json()
    ):
        raise PayloadAssemblyError(identity_issues)
    surfaces = _surface_matrix(platform)
    surface_entries = [
        _entry(payload, surface.identifier, surface.relative_path)
        for surface in surfaces
    ]
    documents = tuple(
        path.relative_to(payload).as_posix()
        for path in sorted((payload / "Documentation").rglob("*"))
        if path.is_file() and not path.is_symlink()
    )
    for required in (
        "Documentation/manual/USER_MANUAL.md",
        "Documentation/support/SUPPORT.md",
        "Documentation/external-beta-documentation.json",
        "THIRD_PARTY_NOTICES.md",
        "Notices/openssl-LICENSE.txt",
        "SBOM.spdx.json",
        "Trust/release-trust-roots.json",
        "Ownership/installer-ownership.json",
        "release-dependency-closure.json",
    ):
        require_payload_path(payload, required)
    if len(documents) < 3:
        raise PayloadAssemblyError(("payload documentation set is incomplete",))
    closure = _read_json(payload / "release-dependency-closure.json")
    if closure.get("status") != "PASS" or closure.get("platform") != platform:
        raise PayloadAssemblyError(
            ("release dependency closure is not a matching PASS",)
        )
    inventory_path = _resource_inventory_path(platform)
    _validate_resource_inventory(payload / inventory_path)
    trust_test_only, trust_issues = validate_release_trust(payload)
    if trust_issues:
        raise PayloadAssemblyError(trust_issues)
    notices = ("THIRD_PARTY_NOTICES.md",) + tuple(
        path.relative_to(payload).as_posix()
        for path in sorted((payload / "Notices").rglob("*"))
        if path.is_file() and not path.is_symlink()
    )
    payload_digest = tree_sha256(payload, {MANIFEST_NAME})
    manifest_path = payload / MANIFEST_NAME
    manifest = {
        "schemaVersion": 1,
        "purpose": "project-seam-release-payload",
        "platform": platform,
        "releaseIdentity": identity.to_json(),
        "identitySha256": tree_sha256(payload / "RELEASE_IDENTITY.json"),
        "sourceClean": True,
        "surfaces": surface_entries,
        "documents": [
            _entry(payload, f"document-{index + 1}", relative)
            for index, relative in enumerate(documents)
        ],
        "notices": [
            _entry(payload, f"notice-{index + 1}", relative)
            for index, relative in enumerate(notices)
        ],
        "sbom": _entry(payload, "sbom", "SBOM.spdx.json"),
        "trustInputs": [
            _entry(payload, "release-trust-roots", "Trust/release-trust-roots.json"),
            _entry(
                payload, "update-root-public-key", "Trust/update-root-public-key.json"
            ),
        ],
        "ownership": _entry(
            payload, "installer-ownership", "Ownership/installer-ownership.json"
        ),
        "dependencyClosure": _entry(
            payload, "dependency-closure", "release-dependency-closure.json"
        ),
        "bankSidecar": {
            "distribution": "external-per-user",
            "bundledVoicebanks": [],
            "resourceInventory": inventory_path,
            "resourceInventorySha256": tree_sha256(payload / inventory_path),
        },
        "developmentTrustOnly": trust_test_only,
        "releaseEligible": False,
        "payloadTreeSha256": payload_digest,
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return PayloadManifest(
        platform=platform,
        path=manifest_path,
        surface_ids=tuple(surface.identifier for surface in surfaces),
        payload_sha256=payload_digest,
    )
