#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any

ALLOWED_LICENSES = {"MIT", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Zlib"}
COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$")


def validate_lock(lock: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if lock.get("schemaVersion") != 1:
        errors.append("dependency lock schemaVersion must equal 1")
    dependencies = lock.get("dependencies")
    if not isinstance(dependencies, list) or not dependencies:
        return errors + ["dependencies must be a non-empty array"]
    names: set[str] = set()
    for index, dep in enumerate(dependencies):
        if not isinstance(dep, dict):
            errors.append(f"dependencies[{index}] must be an object")
            continue
        name = dep.get("name")
        if not isinstance(name, str) or not name:
            errors.append(f"dependencies[{index}] requires a name")
            continue
        if name in names:
            errors.append(f"duplicate dependency name: {name}")
        names.add(name)
        commit = dep.get("commit")
        if not isinstance(commit, str) or not COMMIT_PATTERN.fullmatch(commit):
            errors.append(f"{name}: commit must be a lowercase 40-character SHA-1")
        repository = dep.get("repository")
        if not isinstance(repository, str) or not repository.startswith("https://github.com/") or not repository.endswith(".git"):
            errors.append(f"{name}: repository must be an explicit HTTPS GitHub .git URL")
        tag = dep.get("tag")
        if not isinstance(tag, str) or not tag or tag in {"main", "master", "HEAD"}:
            errors.append(f"{name}: tag must be an immutable release/tag label")
        license_name = dep.get("license")
        if license_name not in ALLOWED_LICENSES:
            errors.append(f"{name}: license {license_name!r} is not allowed")
    return errors


def _git_head(checkout: Path) -> str | None:
    if not (checkout / ".git").exists():
        return None
    completed = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    return completed.stdout.strip() if completed.returncode == 0 else None


def validate_checkout(dep: dict[str, Any], checkout: Path) -> list[str]:
    errors: list[str] = []
    if not checkout.is_dir():
        return [f"{dep['name']}: checkout directory does not exist: {checkout}"]
    revision_file = checkout / ".phase13a-revision"
    head = _git_head(checkout)
    if head is None and revision_file.is_file():
        head = revision_file.read_text(encoding="utf-8").strip()
    if head != dep["commit"]:
        errors.append(f"{dep['name']}: checkout revision {head!r} differs from locked {dep['commit']}")
    license_candidates = [checkout / "LICENSE", checkout / "LICENSE.txt", checkout / "COPYING"]
    if not any(path.is_file() and path.stat().st_size > 0 for path in license_candidates):
        errors.append(f"{dep['name']}: checkout is missing a non-empty license file")
    if dep.get("recursiveSubmodules") and (checkout / ".git").exists():
        completed = subprocess.run(
            ["git", "-C", str(checkout), "submodule", "status", "--recursive"],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if completed.returncode != 0 or any(line.startswith(("-", "+", "U")) for line in completed.stdout.splitlines()):
            errors.append(f"{dep['name']}: recursive submodules are not initialized at locked revisions")
    return errors


def load_lock(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("dependency lock root must be an object")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate Phase 13A SDK lock and audited checkouts")
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--checkout-root", type=Path)
    args = parser.parse_args(argv)
    try:
        lock = load_lock(args.lock)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(exc)
        return 2
    errors = validate_lock(lock)
    if args.checkout_root and not errors:
        for dep in lock["dependencies"]:
            errors.extend(validate_checkout(dep, args.checkout_root / dep["name"]))
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 3
    print("PHASE13A_SDK_LOCK=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
