from __future__ import annotations

import json
import platform
import shutil
from pathlib import Path

from tools.phase13a.dependency_closure import (
    CommandBinaryInspector,
    dependency_closure_json,
    verify_dependency_closure,
)
from tools.phase13a.distribution_manifest import (
    build_wrapper_manifest,
    validate_artifact,
    validate_wrapper_bundle,
)
from tools.phase13a.release_payload import (
    PayloadPlatform,
    assemble_release_payload,
)


CONFIGURATION_DIRECTORIES = frozenset(
    {"debug", "release", "relwithdebinfo", "minsizerel"}
)


def first_artifact(root: Path, name: str, configuration: str) -> Path:
    found = [path for path in root.rglob(name) if path.exists()]
    if not found:
        raise FileNotFoundError(f"{name} not found below {root}")
    configured = [
        (
            path,
            next(
                (
                    part.casefold()
                    for part in path.relative_to(root).parts[:-1]
                    if part.casefold() in CONFIGURATION_DIRECTORIES
                ),
                None,
            ),
        )
        for path in found
    ]
    if any(
        candidate_configuration is not None for _, candidate_configuration in configured
    ):
        found = [
            path
            for path, candidate_configuration in configured
            if candidate_configuration == configuration.casefold()
        ]
        if not found:
            raise RuntimeError(
                f"{name} not found for configuration {configuration} below {root}"
            )
    return sorted(found, key=lambda path: (len(path.parts), str(path)))[0]


def first_artifact_from(roots: tuple[Path, ...], name: str, configuration: str) -> Path:
    for root in roots:
        try:
            return first_artifact(root, name, configuration)
        except FileNotFoundError:
            continue
    joined = ", ".join(str(root) for root in roots)
    raise FileNotFoundError(f"{name} not found below any of: {joined}")


def copy_artifact(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination) if destination.is_dir() else destination.unlink()
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination) if source.is_dir() else shutil.copy2(
        source, destination
    )


def materialize_clap(
    project_build: Path, output: Path, host_system: str, configuration: str
) -> tuple[Path, Path]:
    source = first_artifact(project_build, "ProjectSEAMEditor.clap", configuration)
    destination = output / "CLAP" / source.name
    copy_artifact(source, destination)
    source_sidecar = source.parent / "ProjectSEAMEditor.resources"
    if source.is_file():
        resources = destination.parent / "ProjectSEAMEditor.resources"
        if source_sidecar.is_dir():
            copy_artifact(source_sidecar, resources)
    else:
        resources = destination / "Contents" / "Resources"
    errors = validate_artifact("clap", destination, host_system)
    if errors:
        raise RuntimeError("; ".join(errors))
    return destination, resources


def materialize_vst3(
    wrapper_build: Path,
    output: Path,
    canonical_clap_sha256: str,
    host_system: str,
    version: str,
    release_identity: dict[str, str | int],
    configuration: str,
) -> Path:
    source = first_artifact_from(
        (wrapper_build, output), "ProjectSEAMEditor.vst3", configuration
    )
    destination = output / "VST3" / source.name
    copy_artifact(source, destination)
    manifest_path = destination / "wrapper-manifest.json"
    if destination.is_dir() and host_system.casefold() in {"darwin", "macos"}:
        manifest_path = destination / "Contents/Resources/wrapper-manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest = build_wrapper_manifest(
        "VST3",
        host_system.casefold(),
        platform.machine(),
        version,
        "com.project-seam.editor.vst3",
        canonical_clap_sha256,
        destination,
        release_identity,
    )
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    errors = validate_wrapper_bundle(
        "VST3", destination, host_system.casefold(), canonical_clap_sha256
    )
    if errors:
        raise RuntimeError("; ".join(errors))
    return destination


def materialize_auv2(
    wrapper_build: Path,
    output: Path,
    canonical_clap_sha256: str,
    version: str,
    release_identity: dict[str, str | int],
    configuration: str,
) -> Path:
    source = first_artifact_from(
        (wrapper_build, output), "ProjectSEAMEditor.component", configuration
    )
    generated_info = (
        wrapper_build / "ProjectSEAMEditorAUv2-build-helper-output" / "auv2_Info.plist"
    )
    if generated_info.is_file():
        shutil.copy2(generated_info, source / "Contents/Info.plist")
    destination = output / "AU" / source.name
    copy_artifact(source, destination)
    generated_copy = output / source.name
    if generated_copy != destination and generated_copy.exists():
        shutil.rmtree(generated_copy)
    manifest_path = destination / "Contents/Resources/wrapper-manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest = build_wrapper_manifest(
        "AUv2",
        "macos",
        platform.machine(),
        version,
        "com.project-seam.editor.auv2",
        canonical_clap_sha256,
        destination,
        release_identity,
    )
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    errors = validate_wrapper_bundle(
        "auv2", destination, "macos", canonical_clap_sha256
    )
    if errors:
        raise RuntimeError("; ".join(errors))
    return destination


def materialize_identity_sidecars(
    output: Path,
    platform: PayloadPlatform,
) -> None:
    source = output / "RELEASE_IDENTITY.json"
    destinations = (
        (
            output
            / "Standalone/Project SEAM.app/Contents/Resources/RELEASE_IDENTITY.json",
            output
            / "CLAP/ProjectSEAMEditor.clap/Contents/Resources/RELEASE_IDENTITY.json",
        )
        if platform is PayloadPlatform.MACOS_ARM64
        else (
            output / "Standalone/Resources/RELEASE_IDENTITY.json",
            output / "CLAP/ProjectSEAMEditor.resources/RELEASE_IDENTITY.json",
        )
    )
    for destination in destinations:
        copy_artifact(source, destination)


def materialize_native_surfaces(
    project_build: Path,
    output: Path,
    platform: PayloadPlatform,
    configuration: str,
) -> None:
    if platform is PayloadPlatform.MACOS_ARM64:
        copy_artifact(
            first_artifact(project_build, "Project SEAM.app", configuration),
            output / "Standalone/Project SEAM.app",
        )
        copy_artifact(
            first_artifact(project_build, "seam_installer_verifier", configuration),
            output / "Tools/seam_installer_verifier",
        )
    else:
        standalone = first_artifact(
            project_build, "seam_editor_native.exe", configuration
        )
        copy_artifact(standalone, output / "Standalone/seam_editor_native.exe")
        copy_artifact(standalone.parent / "Resources", output / "Standalone/Resources")
        copy_artifact(
            first_artifact(project_build, "seam_installer_verifier.exe", configuration),
            output / "Tools/seam_installer_verifier.exe",
        )


def materialize_release_inputs(
    source_root: Path,
    output: Path,
    platform: PayloadPlatform,
) -> None:
    for directory in (output / "Trust", output / "Ownership"):
        if directory.is_symlink() or directory.is_file():
            directory.unlink()
        elif directory.exists():
            shutil.rmtree(directory)
    copy_artifact(
        source_root / "packaging/trust/release-trust-roots.json",
        output / "Trust/release-trust-roots.json",
    )
    copy_artifact(
        source_root / "packaging/trust/update-root-public-key.json",
        output / "Trust/update-root-public-key.json",
    )
    ownership = {
        PayloadPlatform.MACOS_ARM64: "packaging/macos/installer-ownership.json",
        PayloadPlatform.WINDOWS_X64: "packaging/windows/installer-ownership.json",
    }[platform]
    copy_artifact(
        source_root / ownership,
        output / "Ownership/installer-ownership.json",
    )


def seal_platform_payload(
    source_root: Path,
    output: Path,
    platform: PayloadPlatform,
    openssl_commit: str,
) -> None:
    report = verify_dependency_closure(
        output, platform, CommandBinaryInspector(platform), openssl_commit
    )
    (output / "release-dependency-closure.json").write_text(
        json.dumps(dependency_closure_json(report), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if report.status != "PASS":
        raise RuntimeError("; ".join(report.errors))
    assemble_release_payload(output, source_root, platform)
