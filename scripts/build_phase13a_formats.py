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
from distribution_manifest import build_release_manifest, tree_sha256, validate_artifact
from sdk_lock import load_lock, validate_checkout, validate_lock


def run(command: list[object], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(map(str, command)))
    subprocess.run(list(map(str, command)), env=env, check=True)


def first(root: Path, name: str) -> Path:
    found = [path for path in Path(root).rglob(name) if path.exists()]
    if not found:
        raise FileNotFoundError(f"{name} not found below {root}")
    return sorted(found, key=lambda path: (len(path.parts), str(path)))[0]


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
    args = parser.parse_args(argv)

    source_root = args.source_root.resolve()
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
    if args.auv2 and host_system != "Darwin":
        errors.append("AUv2 target build requires macOS")
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
            raise RuntimeError("; ".join(clap_errors))

        wrapper_arguments: list[object] = [
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

        vst3_source = first(wrapper_build, "ProjectSEAMEditor.vst3")
        vst3_output = output / "VST3" / vst3_source.name
        copy_artifact(vst3_source, vst3_output)
        vst3_errors = validate_artifact("vst3", vst3_output, host_system)
        if vst3_errors:
            raise RuntimeError("; ".join(vst3_errors))

        artifacts = [
            {"format": "CLAP", "path": str(clap_output), "sha256": tree_sha256(clap_output)},
            {"format": "VST3", "path": str(vst3_output), "sha256": tree_sha256(vst3_output)},
        ]
        if args.auv2:
            au_source = first(wrapper_build, "ProjectSEAMEditor.component")
            au_output = output / "AU" / au_source.name
            copy_artifact(au_source, au_output)
            au_errors = validate_artifact("auv2", au_output, "macos")
            if au_errors:
                raise RuntimeError("; ".join(au_errors))
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

        result = {
            "schemaVersion": 1,
            "platform": platform.platform(),
            "configuration": args.configuration,
            "dependencyCommits": {item["name"]: item["commit"] for item in lock["dependencies"]},
            "artifacts": artifacts,
            "validation": {"vst3-validator": "NOT_RUN", "auval": "NOT_RUN"},
            "releaseEligible": False,
        }
        result["releaseManifest"] = build_release_manifest(
            "0.13.0", artifacts, result["validation"]
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
