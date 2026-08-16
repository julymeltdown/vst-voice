#!/usr/bin/env python3
"""Build, verify, and collect reproducible Phase 1 evidence."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path


def run(command: list[str], root: Path, capture: bool = False) -> str:
    completed = subprocess.run(
        command,
        cwd=root,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if completed.returncode != 0:
        output = completed.stdout or ""
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(command)}\n{output}")
    return completed.stdout or ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    out = root / "out/phase1"
    evidence = root / "docs/phase1/evidence"
    out.mkdir(parents=True, exist_ok=True)
    evidence.mkdir(parents=True, exist_ok=True)

    if not args.skip_build:
        run(["cmake", "--preset", "dev"], root)
        run(["cmake", "--build", "--preset", "dev"], root)

    test_output = run([str(root / "build/dev/seam_tests")], root, capture=True)
    (evidence / "test-output.txt").write_text(test_output, encoding="utf-8")
    run([str(root / "build/dev/seam_phase1_demo"), "--output", str(out)], root)

    benchmark = run([str(root / "build/dev/seam_phase1_benchmark")], root, capture=True)
    parsed_benchmark = json.loads(benchmark)
    (evidence / "phase1-benchmark.json").write_text(
        json.dumps(parsed_benchmark, indent=2) + "\n", encoding="utf-8"
    )

    branch = run(
        ["python3", "scripts/verify_master_branch.py", "--root", str(root)],
        root,
        capture=True,
    )
    (evidence / "branch-policy.txt").write_text(branch, encoding="utf-8")

    run(["python3", "tools/license-auditor/audit.py", "--root", str(root)], root)

    for filename in ("phase1-piano-roll.svg", "phase1-summary.json"):
        shutil.copy2(out / filename, evidence / filename)

    inkscape = shutil.which("inkscape")
    if inkscape:
        run(
            [
                inkscape,
                str(out / "phase1-piano-roll.svg"),
                "--export-type=png",
                f"--export-filename={out / 'phase1-piano-roll.png'}",
            ],
            root,
        )
        shutil.copy2(out / "phase1-piano-roll.png", evidence / "phase1-piano-roll.png")

    excluded = {".git", "build", "out", ".cache"}
    files: list[str] = []
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if any(part in excluded for part in relative.parts):
            continue
        if path.is_file():
            files.append(relative.as_posix())
    (root / "docs/phase1/FILE_TREE.txt").write_text(
        "\n".join(sorted(files)) + "\n", encoding="utf-8"
    )

    print(f"Phase 1 evidence written to {evidence}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
