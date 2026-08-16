#!/usr/bin/env python3
"""Collect Phase 8 platform-source and Linux regression evidence.

Windows and macOS runtime certification is deliberately delegated to the
repository's target-host CI matrix. This script proves that the packaged Linux
checkout retains the platform source closure and that prior functionality did
not regress.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def run(command: list[str], root: Path, timeout: int = 900) -> str:
    completed = subprocess.run(
        command,
        cwd=root,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"{completed.stdout}"
        )
    return completed.stdout


def write(path: Path, text: str) -> None:
    path.write_text(text if text.endswith("\n") else text + "\n", encoding="utf-8")


def configure_build(root: Path, preset: str) -> None:
    run(["cmake", "--preset", preset], root, 300)
    run(["cmake", "--build", "--preset", preset, "-j4"], root, 1200)


def named_test_count(text: str) -> int:
    match = re.search(r"(\d+) passed,\s*(\d+) failed", text)
    if match is None or int(match.group(2)) != 0:
        raise RuntimeError("named test output did not report zero failures")
    return int(match.group(1))


def ctest_ok(text: str) -> bool:
    return "100% tests passed" in text and "0 tests failed" in text


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    evidence = root / "docs/phase8/evidence"
    output = root / "out/phase8"
    shutil.rmtree(evidence, ignore_errors=True)
    shutil.rmtree(output, ignore_errors=True)
    evidence.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)

    if not args.skip_build:
        for preset in ("dev", "release", "sanitize"):
            configure_build(root, preset)

    direct = run([str(root / "build/dev/seam_tests")], root, 300)
    write(evidence / "direct-tests.txt", direct)
    count = named_test_count(direct)

    ctest: dict[str, str] = {}
    for preset in ("dev", "release", "sanitize"):
        text = run(["ctest", "--preset", preset, "--output-on-failure"], root, 900)
        if not ctest_ok(text):
            raise RuntimeError(f"CTest failed for preset {preset}")
        write(evidence / f"ctest-{preset}.txt", text)
        ctest[preset] = "pass"

    source_contract = run(
        [sys.executable, "scripts/verify_phase8_platform_sources.py", "--root", str(root)],
        root,
        60,
    )
    write(evidence / "platform-source-contract.txt", source_contract)

    demo = run(
        [str(root / "build/dev/seam_phase8_demo"), "--output", str(output)],
        root,
        60,
    )
    write(evidence / "phase8-demo.txt", demo)
    shutil.copy2(output / "phase8-platform-capabilities.json",
                 evidence / "phase8-platform-capabilities.json")

    branch = run([sys.executable, "scripts/verify_master_branch.py", "--root", str(root)], root, 60)
    licenses = run([sys.executable, "tools/license-auditor/audit.py", "--root", str(root)], root, 60)
    fsck = run(["git", "fsck", "--full"], root, 180)
    diff = run(["git", "diff", "--check"], root, 60)
    history = run(["git", "log", "--oneline", "--decorate", "-40"], root, 60)
    write(evidence / "branch-policy.txt", branch)
    write(evidence / "license-audit.txt", licenses)
    write(evidence / "git-fsck.txt", fsck or "git fsck --full: pass")
    write(evidence / "git-diff-check.txt", diff or "git diff --check: pass")
    write(evidence / "git-history.txt", history)

    capabilities = json.loads(
        (output / "phase8-platform-capabilities.json").read_text(encoding="utf-8")
    )
    matrix = {
        "phase": "8.0",
        "applicationVersion": "0.8.0",
        "branchPolicy": "master-only-pass",
        "namedTests": {"passed": count, "failed": 0},
        "ctest": ctest,
        "localRuntimeHost": capabilities,
        "windows": {
            "sourceImplementation": "Win32 + native EDIT/TSF services + WASAPI output/capture",
            "sourceContract": "pass",
            "targetHostWorkflow": "configured",
            "runtimeCertifiedInThisLinuxContainer": False,
        },
        "macOS": {
            "sourceImplementation": "AppKit + NSTextInputClient + CoreAudio output/HAL capture",
            "sourceContract": "pass",
            "targetHostWorkflow": "configured",
            "runtimeCertifiedInThisLinuxContainer": False,
        },
        "characterBoundary": "presentation-only; unchanged across native backends",
    }
    (evidence / "verification-matrix.json").write_text(
        json.dumps(matrix, indent=2) + "\n", encoding="utf-8"
    )

    hashes = {
        path.name: digest(path)
        for path in sorted(evidence.iterdir())
        if path.is_file() and path.name != "SHA256SUMS.json"
    }
    (evidence / "SHA256SUMS.json").write_text(
        json.dumps(hashes, indent=2) + "\n", encoding="utf-8"
    )

    excluded = {".git", "build", "out", ".cache", "__pycache__"}
    files: list[str] = []
    for directory, names, filenames in os.walk(root):
        names[:] = sorted(name for name in names if name not in excluded)
        base = Path(directory)
        for filename in sorted(filenames):
            files.append(str((base / filename).relative_to(root)))
    write(root / "docs/phase8/FILE_TREE.txt", "\n".join(files))
    print(json.dumps(matrix, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
