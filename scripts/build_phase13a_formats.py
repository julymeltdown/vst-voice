#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
from compatibility_patches import apply_phase13a_patches  # noqa: E402
from distribution_manifest import build_release_manifest, build_wrapper_manifest, read_validation_status, tree_sha256, validate_artifact, validate_wrapper_bundle  # noqa: E402
from release_identity import resolve_release_identity, write_release_identity  # noqa: E402
from sdk_lock import load_lock, validate_checkout, validate_lock  # noqa: E402
from wrapper_preflight import validate_preflight  # noqa: E402


PATCHES = (
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-vst3-sdk-iid.patch"),
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-macos-cfstring-buffer.patch"),
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-clap-path.patch"),
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-vst3-editor-compatibility.patch"),
)


class Phase13ABuildError(RuntimeError):
    pass


def run(command: list[str | Path], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(map(str, command)))
    subprocess.run(list(map(str, command)), env=env, check=True)


def first(root: Path, name: str) -> Path:
    found = [path for path in Path(root).rglob(name) if path.exists()]
    if not found:
        raise FileNotFoundError(f"{name} not found below {root}")
    return sorted(found, key=lambda path: (len(path.parts), str(path)))[0]


def first_from(roots: tuple[Path, ...], name: str) -> Path:
    for root in roots:
        try:
            return first(root, name)
        except FileNotFoundError:
            continue
    joined = ", ".join(str(root) for root in roots)
    raise FileNotFoundError(f"{name} not found below any of: {joined}")


def copy_artifact(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination) if destination.is_dir() else destination.unlink()
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination) if source.is_dir() else shutil.copy2(source, destination)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build Project SEAM CLAP, VST3 and optional AUv2 artifacts")
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--dependencies", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Release")
    parser.add_argument("--auv2", action="store_true")
    parser.add_argument("--version")
    parser.add_argument("--vst3-validation-result", type=Path)
    parser.add_argument("--auval-validation-result", type=Path)
    args = parser.parse_args(argv)

    source_root = args.source_root.resolve()
    identity_environment = os.environ | ({"SEAM_VERSION": args.version} if args.version is not None else {})
    identity = resolve_release_identity(source_root, identity_environment)
    version = identity.version
    dependencies = args.dependencies.resolve()
    build_root = args.build_root.resolve()
    output = (args.output or build_root / "artifacts").resolve()
    lock = load_lock(source_root / "phase13a" / "dependency-lock.json")
    errors = validate_lock(lock)
    for dependency in lock["dependencies"]:
        if dependency["name"] == "AudioUnitSDK" and not args.auv2:
            continue
        errors.extend(validate_checkout(dependency, dependencies / dependency["name"]))
    host_system = platform.system()
    applied_patches = apply_phase13a_patches(source_root, dependencies, host_system, PATCHES)
    if args.auv2 and host_system != "Darwin":
        errors.append("AUv2 target build requires macOS")
    target_name = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}.get(host_system, host_system.lower())
    architecture = {"AMD64": "x64", "aarch64": "arm64"}.get(platform.machine(), platform.machine())
    preflight_errors, _ = validate_preflight(
        lock=lock,
        dependencies=dependencies,
        wrapper_project=source_root / "packaging/phase13a/wrapper-project/CMakeLists.txt",
        target=target_name,
        architecture=architecture,
        version=identity.version,
        build_id=identity.build_id,
        source_commit=identity.source_commit,
        auv2=args.auv2,
    )
    errors.extend(preflight_errors)
    if errors:
        for error in errors:
            print("ERROR:", error, file=sys.stderr)
        return 3

    try:
        project_build = build_root / "project"
        wrapper_build = build_root / "wrapper"
        output.mkdir(parents=True, exist_ok=True)
        run([
            "cmake", "-S", source_root, "-B", project_build,
            f"-DCMAKE_BUILD_TYPE={args.configuration}",
            f"-DSEAM_BUILD_ID={identity.build_id}",
            f"-DSEAM_SOURCE_COMMIT={identity.source_commit}",
            f"-DSEAM_BUILD_EPOCH={identity.build_epoch}",
            "-DSEAM_BUILD_TESTS=OFF", "-DSEAM_BUILD_BENCHMARKS=OFF",
        ])
        run([
            "cmake", "--build", project_build,
            "--target", "seam_clap_editor_plugin", "--parallel", "2",
        ])
        clap_source = first(project_build, "ProjectSEAMEditor.clap")
        clap_output = output / "CLAP" / clap_source.name
        copy_artifact(clap_source, clap_output)
        source_sidecar = clap_source.parent / "ProjectSEAMEditor.resources"
        if clap_source.is_file():
            resource_directory = clap_output.parent / "ProjectSEAMEditor.resources"
            if source_sidecar.is_dir():
                copy_artifact(source_sidecar, resource_directory)
        else:
            resource_directory = clap_output / "Contents" / "Resources"
        clap_errors = validate_artifact("clap", clap_output, host_system)
        if clap_errors:
            raise Phase13ABuildError("; ".join(clap_errors))

        wrapper_arguments: list[str | Path] = [
            "cmake",
            "-S", source_root / "packaging" / "phase13a" / "wrapper-project",
            "-B", wrapper_build,
            f"-DCMAKE_BUILD_TYPE={args.configuration}",
            f"-DCLAP_WRAPPER_ROOT={dependencies / 'clap-wrapper'}",
            f"-DCLAP_SDK_ROOT={dependencies / 'clap'}",
            f"-DVST3_SDK_ROOT={dependencies / 'vst3sdk'}",
            f"-DSEAM_CLAP_MODULE={clap_output}",
            f"-DSEAM_RESOURCE_DIR={resource_directory}",
            f"-DSEAM_ARTIFACT_DIR={output}",
            f"-DSEAM_FORMAT_VERSION={version}",
            "-DCLAP_SUPPORTS_ALL_NOTE_EXPRESSIONS=ON",
            "-DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=OFF",
            "-DCLAP_WRAPPER_COPY_AFTER_BUILD=OFF",
            f"-DCLAP_WRAPPER_BUILD_AUV2={'ON' if args.auv2 else 'OFF'}",
            f"-DSEAM_BUILD_AUV2={'ON' if args.auv2 else 'OFF'}",
        ]
        if args.auv2:
            wrapper_arguments.append(f"-DAUDIOUNIT_SDK_ROOT={dependencies / 'AudioUnitSDK'}")
        environment = os.environ.copy()
        environment["CLAP_PATH"] = str(clap_output.parent)
        run(wrapper_arguments, environment)
        run([
            "cmake", "--build", wrapper_build,
            "--config", args.configuration, "--parallel", "2",
        ], environment)

        vst3_source = first_from((wrapper_build, output), "ProjectSEAMEditor.vst3")
        vst3_output = output / "VST3" / vst3_source.name
        copy_artifact(vst3_source, vst3_output)
        vst3_manifest_location = vst3_output / "wrapper-manifest.json"
        if vst3_output.is_dir() and host_system.lower() in {"darwin", "macos"}:
            vst3_manifest_location = vst3_output / "Contents" / "Resources" / "wrapper-manifest.json"
        vst3_manifest_location.parent.mkdir(parents=True, exist_ok=True)
        vst3_manifest = build_wrapper_manifest("VST3", host_system.lower(), platform.machine(), version, "com.project-seam.editor.vst3", tree_sha256(clap_output), vst3_output)
        vst3_manifest_location.write_text(json.dumps(vst3_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        vst3_errors = validate_wrapper_bundle("VST3", vst3_output, host_system.lower(), tree_sha256(clap_output))
        if vst3_errors:
            raise Phase13ABuildError("; ".join(vst3_errors))

        artifacts = [
            {"format": "CLAP", "path": str(clap_output), "sha256": tree_sha256(clap_output)},
            {"format": "VST3", "path": str(vst3_output), "sha256": tree_sha256(vst3_output), "canonicalClapSha256": tree_sha256(clap_output)},
        ]
        if args.auv2:
            au_source = first_from(
                (wrapper_build, output), "ProjectSEAMEditor.component")
            generated_au_info = (
                wrapper_build
                / "ProjectSEAMEditorAUv2-build-helper-output"
                / "auv2_Info.plist"
            )
            if generated_au_info.is_file():
                shutil.copy2(
                    generated_au_info,
                    au_source / "Contents" / "Info.plist",
                )
            au_output = output / "AU" / au_source.name
            copy_artifact(au_source, au_output)
            au_manifest_location = au_output / "Contents" / "Resources" / "wrapper-manifest.json"
            au_manifest_location.parent.mkdir(parents=True, exist_ok=True)
            au_manifest = build_wrapper_manifest("AUv2", "macos", platform.machine(), version, "com.project-seam.editor.auv2", tree_sha256(clap_output), au_output)
            au_manifest_location.write_text(json.dumps(au_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            au_errors = validate_wrapper_bundle("auv2", au_output, "macos", tree_sha256(clap_output))
            if au_errors:
                raise Phase13ABuildError("; ".join(au_errors))
            artifacts.append({"format": "AUv2", "path": str(au_output), "sha256": tree_sha256(au_output)})

        notices = output / "Notices"
        notices.mkdir(exist_ok=True)
        for dependency in lock["dependencies"]:
            if dependency["name"] == "AudioUnitSDK" and not args.auv2:
                continue
            dependency_root = dependencies / dependency["name"]
            for licence in list(dependency_root.glob("LICENSE*")) + list(dependency_root.glob("COPYING*")):
                if licence.is_file():
                    shutil.copy2(licence, notices / f"{dependency['name']}-{licence.name}")
        for notice in ("THIRD_PARTY_NOTICES.md", "SBOM.spdx.json"):
            shutil.copy2(source_root / notice, output / notice)
        for documentation_root in (source_root / "docs" / "manual", source_root / "docs" / "support"):
            destination_root = output / "Documentation" / documentation_root.name
            destination_root.mkdir(parents=True, exist_ok=True)
            for document in sorted(documentation_root.glob("*.md")):
                shutil.copy2(document, destination_root / document.name)
        documentation_manifest = source_root / "docs" / "product" / "external-beta-documentation.json"
        shutil.copy2(documentation_manifest, output / "Documentation" / documentation_manifest.name)
        write_release_identity(output / "RELEASE_IDENTITY.json", identity)

        validation = {"vst3-validator": "NOT_RUN", "auval": "NOT_RUN"}
        validation_evidence: dict[str, str] = {}
        if args.vst3_validation_result is not None:
            validation["vst3-validator"] = read_validation_status(
                args.vst3_validation_result,
                expected_artifact_sha256=tree_sha256(vst3_output),
                artifact_hash_field="pluginSha256",
            )
            validation_evidence["vst3-validator"] = str(args.vst3_validation_result.resolve())
        if args.auval_validation_result is not None:
            if not args.auv2:
                raise Phase13ABuildError("AUv2 validation evidence requires --auv2")
            validation["auval"] = read_validation_status(
                args.auval_validation_result,
                expected_artifact_sha256=tree_sha256(au_output),
                artifact_hash_field="componentSha256",
            )
            validation_evidence["auval"] = str(args.auval_validation_result.resolve())

        result = {
            "schemaVersion": 1,
            "platform": platform.platform(),
            "configuration": args.configuration,
            "version": version,
            "releaseIdentity": identity.as_dict(),
            "dependencyCommits": {item["name"]: item["commit"] for item in lock["dependencies"]},
            "dependencyPatches": applied_patches,
            "artifacts": artifacts,
            "validation": validation,
            "validationEvidence": validation_evidence,
            "releaseEligible": False,
        }
        result["releaseManifest"] = build_release_manifest(
            version, artifacts, result["validation"], validation_evidence
        )
        (build_root / "phase13a-build-result.json").write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        print("ERROR:", exc, file=sys.stderr)
        return 4


if __name__ == "__main__":
    raise SystemExit(main())
