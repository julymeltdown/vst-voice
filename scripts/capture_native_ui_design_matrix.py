#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
from typing import Any


VIEWPORTS = (
    ("compact", 480, 320),
    ("small", 720, 450),
    ("medium", 960, 600),
    ("wide", 1188, 768),
    ("desktop", 1280, 800),
    ("large", 1440, 900),
)
ZOOMS = (25, 50, 100, 200)
SCALES = (1, 2)
JOURNEY_CAPTURES = (
    "note-detail-focus.ppm",
    "overlap-cycle-before.ppm",
    "overlap-cycle-1.ppm",
    "overlap-cycle-2.ppm",
    "character-ready-matched.ppm",
    "character-ready-mismatched.ppm",
    "lane-transition-start.ppm",
    "lane-transition-mid.ppm",
    "lane-transition-end.ppm",
    "lane-reduced-motion-final.ppm",
    "identity-transition-start.ppm",
    "identity-transition-mid.ppm",
    "identity-transition-end.ppm",
)


class EvidenceError(RuntimeError):
    pass


def expected_capture_names() -> tuple[str, ...]:
    return tuple(
        f"dense-{name}-scale{scale}-zoom{zoom}.ppm"
        for name, _width, _height in VIEWPORTS
        for zoom in ZOOMS
        for scale in SCALES
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def capture_snapshot(captures: Path, expected: tuple[str, ...]) -> dict[str, str]:
    found = {path.name for path in captures.glob("*.ppm")}
    if found != set(expected):
        missing = sorted(set(expected) - found)
        unexpected = sorted(found - set(expected))
        raise EvidenceError(
            f"capture matrix mismatch; missing={missing}, unexpected={unexpected}"
        )
    snapshot: dict[str, str] = {}
    for name in expected:
        path = captures / name
        if path.stat().st_size == 0:
            raise EvidenceError(f"empty capture: {path}")
        snapshot[name] = sha256(path)
    return snapshot


def command_output(command: list[str]) -> str:
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return "unavailable"
    if completed.returncode != 0:
        return "unavailable"
    return completed.stdout.strip() or "unavailable"


def environment_metadata() -> dict[str, Any]:
    page_size = os.sysconf("SC_PAGE_SIZE")
    page_count = os.sysconf("SC_PHYS_PAGES")
    return {
        "os": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor() or "unavailable",
        "physicalMemoryBytes": page_size * page_count,
        "power": command_output(["pmset", "-g", "batt"]),
        "fontIdentity": command_output(["fc-match", "sans-serif"]),
        "cacheState": "cold process; warm second deterministic capture pass",
    }


def git_commit(root: Path) -> str:
    completed = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    commit = completed.stdout.strip()
    if completed.returncode != 0 or len(commit) != 40:
        raise EvidenceError("unable to resolve the candidate commit")
    return commit


def run_capture(
    test_binary: Path, root: Path, captures: Path, journeys: Path
) -> None:
    environment = os.environ.copy()
    environment["SEAM_NATIVE_UI_DESIGN_CAPTURE_DIR"] = str(captures)
    environment["SEAM_NATIVE_UI_JOURNEY_CAPTURE_DIR"] = str(journeys)
    completed = subprocess.run(
        [str(test_binary)], cwd=root, env=environment, check=False
    )
    if completed.returncode != 0:
        raise EvidenceError(f"native UI test binary failed with {completed.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    build_dir = args.build_dir.resolve()
    output = args.output.resolve()
    test_binary = build_dir / "seam_tests"
    if not test_binary.is_file():
        raise EvidenceError(f"missing native UI test binary: {test_binary}")
    if output.exists() and any(output.iterdir()):
        raise EvidenceError(f"output must be empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    captures = output / "captures"
    captures.mkdir(exist_ok=False)
    journeys = output / "journeys"
    journeys.mkdir(exist_ok=False)

    run_capture(test_binary, root, captures, journeys)
    first = capture_snapshot(captures, expected_capture_names())
    first_journeys = capture_snapshot(journeys, JOURNEY_CAPTURES)
    run_capture(test_binary, root, captures, journeys)
    second = capture_snapshot(captures, expected_capture_names())
    second_journeys = capture_snapshot(journeys, JOURNEY_CAPTURES)
    if first != second or first_journeys != second_journeys:
        changed = sorted(name for name in first if first[name] != second[name])
        changed += sorted(
            name
            for name in first_journeys
            if first_journeys[name] != second_journeys[name]
        )
        raise EvidenceError(f"non-deterministic captures: {changed}")

    metadata = environment_metadata()
    manifest = {
        "schema": 1,
        "candidateCommit": git_commit(root),
        "testBinary": str(test_binary),
        "viewports": [
            {"id": name, "width": width, "height": height}
            for name, width, height in VIEWPORTS
        ],
        "timelineZooms": list(ZOOMS),
        "backingScales": list(SCALES),
        "environment": metadata,
        "captures": [
            {
                "path": f"captures/{name}",
                "sha256": first[name],
                "bytes": (captures / name).stat().st_size,
            }
            for name in expected_capture_names()
        ],
        "journeyCaptures": [
            {
                "path": f"journeys/{name}",
                "sha256": first_journeys[name],
                "bytes": (journeys / name).stat().st_size,
            }
            for name in JOURNEY_CAPTURES
        ],
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(output / "manifest.json")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except EvidenceError as error:
        print(f"native UI design matrix: {error}", file=sys.stderr)
        raise SystemExit(1)
