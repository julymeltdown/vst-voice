#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.phase13a.sdk_lock import (  # noqa: E402
    load_lock,
    validate_checkout,
    validate_lock,
)


EXPECTED_DEPENDENCIES = {
    "clap",
    "clap-wrapper",
    "vst3sdk",
    "AudioUnitSDK",
    "openssl",
}
ARCHITECTURES = {"arm64", "x86_64", "x64", "aarch64"}


def _tool_identity(command: str) -> dict[str, Any]:
    path = shutil.which(command) or command
    try:
        process = subprocess.run([path, "--version"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False, timeout=5)
        return {"command": command, "path": path, "version": process.stdout.splitlines()[0] if process.stdout else "", "exitCode": process.returncode}
    except (OSError, subprocess.SubprocessError) as exc:
        return {"command": command, "path": path, "version": "", "error": str(exc)}


def _architecture_errors(target: str, architecture: str) -> list[str]:
    errors: list[str] = []
    if target == "macos" and architecture not in {"arm64", "aarch64"}:
        errors.append("macOS External Beta wrapper target requires arm64")
    if target == "windows" and architecture not in {"x64", "x86_64"}:
        errors.append("Windows External Beta wrapper target requires x64")
    if architecture not in ARCHITECTURES:
        errors.append(f"unsupported target architecture: {architecture}")
    return errors


def validate_preflight(*, lock: dict[str, Any], dependencies: Path, wrapper_project: Path, target: str, architecture: str, version: str, build_id: str, source_commit: str, auv2: bool, cmake: str = "cmake", compiler: str | None = None) -> tuple[list[str], dict[str, Any]]:
    errors = validate_lock(lock)
    dependency_names = {str(item.get("name")) for item in lock.get("dependencies", [])}
    if dependency_names != EXPECTED_DEPENDENCIES:
        errors.append("dependency lock must contain exactly the five approved build dependencies")
    for dependency in lock.get("dependencies", []):
        if dependency.get("name") == "AudioUnitSDK" and not auv2:
            continue
        errors.extend(validate_checkout(dependency, dependencies / str(dependency.get("name"))))
    if not wrapper_project.is_file():
        errors.append(f"wrapper project is missing: {wrapper_project}")
        wrapper_text = ""
    else:
        wrapper_text = wrapper_project.read_text(encoding="utf-8")
    if "CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF" not in wrapper_text:
        errors.append("wrapper project must disable dependency downloads")
    if "CLAP_WRAPPER_COPY_AFTER_BUILD OFF" not in wrapper_text:
        errors.append("wrapper project must disable post-build copy")
    if "CLAP_WRAPPER_WINDOWS_SINGLE_FILE ON" in wrapper_text or "WINDOWS_FOLDER_VST3 FALSE" in wrapper_text:
        errors.append("Windows VST3 single-file output is forbidden; folder packaging is required")
    if target == "macos" and auv2 and "PREFER_CMAKE_AUV2_CONFIGURATION TRUE" not in wrapper_text:
        errors.append("AUv2 target must use the audited CMake configuration")
    errors.extend(_architecture_errors(target, architecture))
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version):
        errors.append("version must be strict semantic versioning")
    if not build_id or not re.fullmatch(r"[A-Za-z0-9._-]{4,160}", build_id):
        errors.append("build_id must be an explicit generated identity")
    if not re.fullmatch(r"[0-9a-fA-F]{40,64}", source_commit):
        errors.append("source_commit must be a full source identity")
    cmake_identity = _tool_identity(cmake)
    if cmake_identity.get("error") or cmake_identity.get("exitCode") != 0:
        errors.append("cmake is unavailable for wrapper preflight")
    compiler_identity = _tool_identity(compiler) if compiler else {"command": "", "path": "", "version": ""}
    if compiler and compiler_identity.get("error"):
        errors.append(f"compiler is unavailable: {compiler}")
    result = {
        "schemaVersion": 1,
        "purpose": "wrapper-toolchain-preflight",
        "status": "PASS" if not errors else "BLOCKED",
        "target": target,
        "architecture": architecture,
        "version": version,
        "buildId": build_id,
        "sourceCommit": source_commit,
        "auV2": auv2,
        "networkDownloads": False,
        "dependencies": [{"name": item.get("name"), "tag": item.get("tag"), "commit": item.get("commit"), "license": item.get("license")} for item in lock.get("dependencies", [])],
        "tools": {"cmake": cmake_identity, "compiler": compiler_identity},
        "checks": {
            "exactDependencyLock": not any("dependency" in error or "checkout" in error for error in errors),
            "noNetworkFallback": "CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF" in wrapper_text,
            "windowsFolderVst3": "CLAP_WRAPPER_WINDOWS_SINGLE_FILE ON" not in wrapper_text and "WINDOWS_FOLDER_VST3 FALSE" not in wrapper_text,
            "generatedIdentity": bool(version and build_id and source_commit),
            "targetArchitecture": not any("architecture" in error for error in errors),
        },
        "errors": errors,
    }
    return errors, result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Fail-closed Phase 13A wrapper SDK/toolchain preflight")
    parser.add_argument("--lock", type=Path, default=ROOT / "phase13a/dependency-lock.json")
    parser.add_argument("--dependencies", type=Path, required=True)
    parser.add_argument("--wrapper-project", type=Path, default=ROOT / "packaging/phase13a/wrapper-project/CMakeLists.txt")
    parser.add_argument("--target", choices=["macos", "windows", "linux"], required=True)
    parser.add_argument("--architecture", default=platform.machine())
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--auv2", action="store_true")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--compiler")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        lock = load_lock(args.lock)
        errors, result = validate_preflight(lock=lock, dependencies=args.dependencies.resolve(), wrapper_project=args.wrapper_project.resolve(), target=args.target, architecture=args.architecture, version=args.version, build_id=args.build_id, source_commit=args.source_commit, auv2=args.auv2, cmake=args.cmake, compiler=args.compiler)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if not errors else 3
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
