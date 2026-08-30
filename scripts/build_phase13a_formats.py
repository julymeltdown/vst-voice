#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.phase13a.compatibility_patches import apply_phase13a_patches  # noqa: E402
from tools.phase13a.distribution_manifest import (  # noqa: E402
    build_release_manifest,
    read_validation_status,
    tree_sha256,
)
from tools.phase13a.payload_materializer import (  # noqa: E402
    materialize_auv2,
    materialize_clap,
    materialize_identity_sidecars,
    materialize_native_surfaces,
    materialize_release_inputs,
    materialize_vst3,
    seal_platform_payload,
)
from tools.phase13a.release_payload import resolve_payload_platform  # noqa: E402
from tools.phase13a.release_identity import (  # noqa: E402
    resolve_release_identity,
    write_release_identity,
)
from tools.phase13a.release_inputs import materialize_supporting_files  # noqa: E402
from tools.phase13a.sdk_lock import (  # noqa: E402
    load_lock,
    validate_checkout,
    validate_lock,
)
from tools.phase13a.static_openssl import prepare_static_openssl  # noqa: E402
from tools.phase13a.wrapper_preflight import validate_preflight  # noqa: E402


PATCHES = (
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-vst3-sdk-iid.patch"),
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-macos-cfstring-buffer.patch"),
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-clap-path.patch"),
    ("clap-wrapper", "packaging/phase13a/patches/clap-wrapper-vst3-editor-compatibility.patch"),
)


def run(command: list[str | Path], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(map(str, command)))
    subprocess.run(list(map(str, command)), env=env, check=True)


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
    host_machine = platform.machine()
    applied_patches = apply_phase13a_patches(source_root, dependencies, host_system, PATCHES)
    payload_platform, target_errors = resolve_payload_platform(
        host_system, host_machine, args.auv2
    )
    errors.extend(target_errors)
    target_name = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}.get(host_system, host_system.lower())
    architecture = {"AMD64": "x64", "aarch64": "arm64"}.get(host_machine, host_machine)
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
        openssl_dependency = next(item for item in lock["dependencies"] if item["name"] == "openssl")
        openssl_prefix = prepare_static_openssl(
            host_system,
            host_machine,
            dependencies / "openssl",
            build_root,
            str(openssl_dependency["commit"]),
        )
        output.mkdir(parents=True, exist_ok=True)
        write_release_identity(output / "RELEASE_IDENTITY.json", identity)
        run([
            "cmake", "-S", source_root, "-B", project_build,
            f"-DCMAKE_BUILD_TYPE={args.configuration}",
            f"-DSEAM_BUILD_ID={identity.build_id}",
            f"-DSEAM_SOURCE_COMMIT={identity.source_commit}",
            f"-DSEAM_BUILD_EPOCH={identity.build_epoch}",
            f"-DOPENSSL_ROOT_DIR={openssl_prefix}",
            "-DOPENSSL_USE_STATIC_LIBS=TRUE",
            "-DSEAM_REQUIRE_STATIC_OPENSSL=ON",
            f"-DSEAM_OPENSSL_SOURCE_COMMIT={openssl_dependency['commit']}",
            *([f"-DCMAKE_MSVC_RUNTIME_LIBRARY={'MultiThreadedDebug' if args.configuration == 'Debug' else 'MultiThreaded'}"] if host_system == "Windows" else []),
            "-DSEAM_BUILD_TESTS=OFF", "-DSEAM_BUILD_BENCHMARKS=OFF",
        ])
        project_targets = ["seam_clap_editor_plugin"]
        if payload_platform is not None:
            project_targets.extend(["seam_editor_native", "seam_installer_verifier"])
        run([
            "cmake", "--build", project_build,
            "--config", args.configuration,
            "--target", *project_targets, "--parallel", "2",
        ])
        if payload_platform is not None:
            materialize_native_surfaces(
                project_build, output, payload_platform, args.configuration
            )
        clap_output, resource_directory = materialize_clap(
            project_build, output, host_system, args.configuration
        )
        if payload_platform is not None:
            materialize_identity_sidecars(output, payload_platform)
        clap_sha256 = tree_sha256(clap_output)

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
            *([f"-DCMAKE_MSVC_RUNTIME_LIBRARY={'MultiThreadedDebug' if args.configuration == 'Debug' else 'MultiThreaded'}"] if host_system == "Windows" else []),
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

        vst3_output = materialize_vst3(
            wrapper_build,
            output,
            clap_sha256,
            host_system,
            version,
            identity.as_dict(),
            args.configuration,
        )
        vst3_sha256 = tree_sha256(vst3_output)

        artifacts = [
            {"format": "CLAP", "path": str(clap_output), "sha256": clap_sha256},
            {"format": "VST3", "path": str(vst3_output), "sha256": vst3_sha256, "canonicalClapSha256": clap_sha256},
        ]
        au_output: Path | None = None
        au_sha256: str | None = None
        if args.auv2:
            au_output = materialize_auv2(
                wrapper_build,
                output,
                clap_sha256,
                version,
                identity.as_dict(),
                args.configuration,
            )
            au_sha256 = tree_sha256(au_output)
            artifacts.append({"format": "AUv2", "path": str(au_output), "sha256": au_sha256})

        materialize_supporting_files(
            source_root,
            dependencies,
            lock["dependencies"],
            output,
            args.auv2,
        )
        if payload_platform is not None:
            materialize_release_inputs(source_root, output, payload_platform)
            seal_platform_payload(
                source_root,
                output,
                payload_platform,
                str(openssl_dependency["commit"]),
            )

        validation: dict[str, str] = {"vst3-validator": "NOT_RUN", "auval": "NOT_RUN"}
        validation_evidence: dict[str, str] = {}
        if args.vst3_validation_result is not None:
            validation["vst3-validator"] = read_validation_status(
                args.vst3_validation_result,
                expected_artifact_sha256=vst3_sha256,
                artifact_hash_field="pluginSha256",
            )
            validation_evidence["vst3-validator"] = str(args.vst3_validation_result.resolve())
        if args.auval_validation_result is not None:
            if not args.auv2 or au_output is None or au_sha256 is None:
                raise RuntimeError("AUv2 validation evidence requires --auv2")
            validation["auval"] = read_validation_status(
                args.auval_validation_result,
                expected_artifact_sha256=au_sha256,
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
            "releaseManifest": build_release_manifest(
                version, artifacts, validation, validation_evidence
            ),
        }
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
