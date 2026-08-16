#!/usr/bin/env python3
"""Project SEAM dependency, provenance, and branch-policy audit."""

from __future__ import annotations

import argparse
import fnmatch
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


REQUIRED_REPOSITORY_FILES = (
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "SBOM.spdx.json",
    "licenses/allowlist.yml",
    "licenses/denylist.yml",
    "third_party/manifest.yml",
    "docs/licensing/DEPENDENCY_POLICY.md",
    "docs/licensing/REFERENCE_POLICY.md",
    "assets/character-01/PROVENANCE.md",
)

FORBIDDEN_DISTRIBUTED_EXTENSIONS = {
    ".dll",
    ".dylib",
    ".so",
    ".exe",
    ".a",
    ".lib",
    ".jar",
    ".wasm",
    ".pt",
    ".pth",
    ".onnx",
    ".ckpt",
}


def load_json_yaml(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Unable to read JSON-compatible YAML {path}: {error}") from error


def branch_policy(root: Path) -> list[str]:
    if not (root / ".git").exists():
        return ["No .git directory: branch policy could not be verified"]
    completed = subprocess.run(
        [sys.executable, str(root / "scripts/verify_master_branch.py"), "--root", str(root)],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode == 0:
        return []
    return [completed.stderr.strip() or completed.stdout.strip() or "Branch policy failed"]


def license_is_denied(identifier: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(identifier, pattern) for pattern in patterns)


def audit(root: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []

    for relative in REQUIRED_REPOSITORY_FILES:
        if not (root / relative).is_file():
            errors.append(f"required file missing: {relative}")

    if (root / ".gitmodules").exists():
        errors.append(".gitmodules is not allowed without an audited manifest entry")

    try:
        allow = load_json_yaml(root / "licenses/allowlist.yml")
        deny = load_json_yaml(root / "licenses/denylist.yml")
        manifest = load_json_yaml(root / "third_party/manifest.yml")
    except ValueError as error:
        errors.append(str(error))
        return errors, warnings

    allowed = set(allow.get("allowed", []))
    denied_patterns = list(deny.get("deniedPatterns", []))
    dependencies = manifest.get("distributedDependencies", [])
    if not isinstance(dependencies, list):
        errors.append("distributedDependencies must be a list")
        dependencies = []

    names: set[str] = set()
    for dependency in dependencies:
        if not isinstance(dependency, dict):
            errors.append("dependency entry must be an object")
            continue
        name = str(dependency.get("name", ""))
        license_id = str(dependency.get("license", ""))
        if not name:
            errors.append("dependency name is required")
            continue
        if name in names:
            errors.append(f"duplicate dependency: {name}")
        names.add(name)
        if not license_id:
            errors.append(f"{name}: license is required")
        elif license_is_denied(license_id, denied_patterns):
            errors.append(f"{name}: denied license {license_id}")
        elif license_id not in allowed:
            errors.append(f"{name}: license {license_id} is not allowlisted")

        revision = str(dependency.get("revision", ""))
        source_hash = str(dependency.get("sourceSha256", ""))
        license_file = str(dependency.get("licenseFile", ""))
        if not revision or revision in {"latest", "main", "master", "HEAD"}:
            errors.append(f"{name}: immutable revision is required")
        if len(source_hash) != 64 or any(c not in "0123456789abcdefABCDEF" for c in source_hash):
            errors.append(f"{name}: a 64-character SHA-256 source hash is required")
        if not license_file or not (root / license_file).is_file():
            errors.append(f"{name}: license file is missing: {license_file}")
        if dependency.get("usage") == "font" and license_id != "OFL-1.1":
            warnings.append(f"{name}: font usage does not use OFL-1.1; verify manually")
        if license_id == "OFL-1.1" and dependency.get("usage") != "font":
            errors.append(f"{name}: OFL-1.1 is approved only for fonts")

    third_party = root / "third_party"
    allowed_top_level = {"manifest.yml", "README.md"}
    for path in third_party.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(third_party)
        if len(relative.parts) == 1 and relative.name in allowed_top_level:
            continue
        if not dependencies:
            errors.append(f"unmanifested third-party file: third_party/{relative}")
        if path.suffix.lower() in FORBIDDEN_DISTRIBUTED_EXTENSIONS:
            errors.append(f"binary/model requires explicit review: third_party/{relative}")

    errors.extend(branch_policy(root))

    concept_manifest = root / "assets/character-01/concepts/manifest.json"
    try:
        concepts = json.loads(concept_manifest.read_text(encoding="utf-8"))
        if len(concepts.get("concepts", [])) != 3:
            errors.append("Phase 1 character manifest must retain exactly three directions")
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"invalid character concept manifest: {error}")

    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()

    errors, warnings = audit(root)
    for warning in warnings:
        print(f"[license-audit] WARNING: {warning}")
    if errors:
        for error in errors:
            print(f"[license-audit] ERROR: {error}", file=sys.stderr)
        print(f"[license-audit] failed with {len(errors)} error(s)", file=sys.stderr)
        return 1

    print(f"[license-audit] distributedDependencies={len(load_json_yaml(root / 'third_party/manifest.yml').get('distributedDependencies', []))}")
    print("[license-audit] branchPolicy=master-only")
    print("[license-audit] characterDirections=3")
    print("[license-audit] status=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
