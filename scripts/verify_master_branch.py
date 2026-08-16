#!/usr/bin/env python3
"""Verify the repository's master-only branch policy."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run_git(root: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "git command failed")
    return completed.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--allow-no-git", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()

    if not (root / ".git").exists():
        message = f"No .git directory found at {root}"
        if args.allow_no_git:
            print(f"[branch-policy] skipped: {message}")
            return 0
        print(f"[branch-policy] ERROR: {message}", file=sys.stderr)
        return 1

    try:
        current = run_git(root, "branch", "--show-current")
        branches_output = run_git(
            root, "for-each-ref", "--format=%(refname:short)", "refs/heads"
        )
    except RuntimeError as error:
        print(f"[branch-policy] ERROR: {error}", file=sys.stderr)
        return 1

    branches = [line for line in branches_output.splitlines() if line]
    if current != "master":
        print(
            f"[branch-policy] ERROR: current branch is {current!r}, expected 'master'",
            file=sys.stderr,
        )
        return 1
    if branches not in ([], ["master"]):
        print(
            f"[branch-policy] ERROR: local branches are {branches!r}, expected ['master']",
            file=sys.stderr,
        )
        return 1

    print("[branch-policy] current=master")
    print("[branch-policy] localBranches=" + ("master" if branches else "master (unborn)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
