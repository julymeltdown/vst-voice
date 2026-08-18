#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

from sdk_lock import load_lock, validate_checkout, validate_lock


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def _safe_checkout_root(path: Path) -> Path:
    resolved = path.expanduser().resolve()
    if resolved == Path(resolved.anchor):
        raise ValueError("checkout root cannot be a filesystem root")
    if resolved.is_symlink():
        raise ValueError("checkout root cannot be a symbolic link")
    return resolved


def acquire_dependency(dependency: dict[str, object], checkout: Path, refresh: bool) -> None:
    expected = str(dependency["commit"])
    repository = str(dependency["repository"])
    if checkout.exists():
        errors = validate_checkout(dependency, checkout)
        if not errors:
            print(f"PHASE13A_DEPENDENCY_REUSED={dependency['name']}@{expected}")
            return
        if not refresh:
            raise ValueError(
                f"{dependency['name']}: existing checkout is invalid; pass --refresh to replace it: "
                + "; ".join(errors)
            )
        if checkout.is_symlink():
            raise ValueError(f"{dependency['name']}: refusing to remove a symbolic-link checkout")
        shutil.rmtree(checkout)

    checkout.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "init", str(checkout)])
    run(["git", "remote", "add", "origin", repository], cwd=checkout)
    # Fetch the exact immutable object rather than trusting a mutable branch or tag.
    run(["git", "fetch", "--depth", "1", "origin", expected], cwd=checkout)
    run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=checkout)
    actual = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=checkout, text=True
    ).strip()
    if actual != expected:
        raise ValueError(
            f"{dependency['name']}: fetched revision {actual!r} differs from locked {expected}"
        )
    if dependency.get("recursiveSubmodules"):
        run(["git", "submodule", "sync", "--recursive"], cwd=checkout)
        run(["git", "submodule", "update", "--init", "--recursive", "--depth", "1"], cwd=checkout)
    (checkout / ".phase13a-revision").write_text(expected + "\n", encoding="utf-8")
    errors = validate_checkout(dependency, checkout)
    if errors:
        raise ValueError("; ".join(errors))
    print(f"PHASE13A_DEPENDENCY_ACQUIRED={dependency['name']}@{expected}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Acquire and verify exact Phase 13A permissive SDK revisions"
    )
    parser.add_argument(
        "--lock", type=Path, default=ROOT / "phase13a" / "dependency-lock.json"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--refresh", action="store_true")
    args = parser.parse_args(argv)

    try:
        lock = load_lock(args.lock)
        errors = validate_lock(lock)
        if errors:
            raise ValueError("; ".join(errors))
        output = _safe_checkout_root(args.output)
        output.mkdir(parents=True, exist_ok=True)
        if args.verify_only:
            checkout_errors: list[str] = []
            for dependency in lock["dependencies"]:
                checkout_errors.extend(
                    validate_checkout(dependency, output / str(dependency["name"]))
                )
            if checkout_errors:
                raise ValueError("; ".join(checkout_errors))
            print("PHASE13A_DEPENDENCY_CHECKOUTS=PASS")
            return 0
        for dependency in lock["dependencies"]:
            acquire_dependency(
                dependency,
                output / str(dependency["name"]),
                args.refresh,
            )
        print("PHASE13A_DEPENDENCY_ACQUISITION=PASS")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
