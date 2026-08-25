#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import developer_package  # noqa: E402
import linux_package_smoke  # noqa: E402
import release_gate  # noqa: E402
import sdk_lock  # noqa: E402
from release_identity import read_project_version  # noqa: E402


class Phase13AEvidenceError(RuntimeError):
    pass


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def find_clap(build_root: Path) -> tuple[Path, Path]:
    candidates = sorted(build_root.rglob("ProjectSEAMEditor.clap"))
    for candidate in candidates:
        if candidate.is_file() and candidate.stat().st_size > 0:
            resources = candidate.parent / "ProjectSEAMEditor.resources"
            if resources.is_dir():
                return candidate, resources
        if candidate.is_dir():
            resources = candidate / "Contents" / "Resources"
            binary_dir = candidate / "Contents" / "MacOS"
            if resources.is_dir() and binary_dir.is_dir() and any(binary_dir.iterdir()):
                return candidate, resources
    raise FileNotFoundError(f"ProjectSEAMEditor.clap and resources not found below {build_root}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate reproducible local Phase 13A source-ready evidence")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    build_root = args.build_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = read_project_version(root)

    run([sys.executable, "-m", "unittest", "discover", "-s", "tests/phase13a", "-v"], root)
    run([sys.executable, "scripts/verify_phase13a_contracts.py", "--root", str(root)], root)
    lock = sdk_lock.load_lock(root / "phase13a" / "dependency-lock.json")
    lock_errors = sdk_lock.validate_lock(lock)
    if lock_errors:
        raise Phase13AEvidenceError("\n".join(lock_errors))

    if not args.skip_build:
        run(["cmake", "-S", str(root), "-B", str(build_root), "-DCMAKE_BUILD_TYPE=Release"], root)
        run(["cmake", "--build", str(build_root), "--target", "seam_clap_editor_plugin", "--parallel", "2"], root)
    clap, resources = find_clap(build_root)

    package = output / f"ProjectSEAM-{version}-linux-unsigned-development.zip"
    package_manifest = developer_package.create_developer_package(
        clap, resources, package, version
    )
    smoke = linux_package_smoke.run_smoke(package, output / "linux-install-sandbox")
    (output / "linux-developer-package-smoke.json").write_text(
        json.dumps(smoke, indent=2) + "\n", encoding="utf-8")
    if smoke["status"] != "PASS":
        raise Phase13AEvidenceError(
            "Linux developer package install/uninstall smoke failed"
        )

    matrix = release_gate.load_matrix(root / "docs" / "phase13a" / "mandatory-validation-matrix.json")
    gate_results = {gate: release_gate.evaluate_matrix(matrix, gate).as_dict() for gate in ("G2", "G3", "G4", "G5")}
    evidence = {
        "schemaVersion": 1,
        "phase": "13A",
        "implementationState": "CI_CONFIGURED",
        "acceptanceStatus": "BLOCKED",
        "localExecuted": {
            "contractTests": "PASS",
            "sourceContract": "PASS",
            "dependencyLock": "PASS",
            "linuxUnsignedDeveloperPackage": "PASS",
            "linuxInstallUninstallSmoke": "PASS",
        },
        "externalMandatory": {
            "vst3BuildAndValidator": "NOT_RUN",
            "auv2BuildAndAuval": "NOT_RUN",
            "windowsAuthenticode": "NOT_RUN",
            "macosNotarization": "NOT_RUN",
            "cleanOsInstallers": "NOT_RUN",
            "commercialDawMatrix": "NOT_RUN",
            "phase12cExact7200SecondSoak": "NOT_RUN",
        },
        "developerPackage": {
            "path": package.name,
            "manifest": package_manifest,
        },
        "releaseGates": gate_results,
    }
    (output / "phase13a-evidence.json").write_text(
        json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
