#!/usr/bin/env python3
"""Check that every configured CTest entry carries exactly one kind label.

Kinds are unit, journey and contract; the tags gui, slow and process-lifetime and
the domain labels already in use may appear beside them (cmake/SeamTestLabels.cmake
defines the vocabulary). The check reads the configured test list with
`ctest --show-only=json-v1`, so it covers tests added in any CMakeLists.txt,
including platform- and option-gated tests present in this build.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys

KINDS = {"unit", "journey", "contract"}
TAGS = {"gui", "slow", "process-lifetime"}
DOMAIN_LABELS = {"neural-native-experiment", "production-public-release", "voice-model-training"}


def labels_of(test: dict) -> list[str]:
    for prop in test.get("properties", []):
        if prop.get("name") == "LABELS":
            value = prop.get("value") or []
            return [value] if isinstance(value, str) else list(value)
    return []


def check(show: dict) -> list[str]:
    tests = show.get("tests", [])
    errors = [] if tests else ["no tests are configured"]
    for test in tests:
        labels = labels_of(test)
        kinds = sorted(set(labels) & KINDS)
        unknown = sorted(set(labels) - KINDS - TAGS - DOMAIN_LABELS)
        if len(kinds) != 1:
            errors.append(f"{test['name']}: expected exactly one kind label, found {kinds or 'none'}")
        if unknown:
            errors.append(f"{test['name']}: unknown labels {unknown}")
        if len(labels) != len(set(labels)):
            errors.append(f"{test['name']}: repeated labels {labels}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Check CTest kind labels")
    parser.add_argument("--ctest", required=True, help="ctest executable")
    parser.add_argument("--build-dir", required=True, help="configured build directory")
    parser.add_argument("--show-only-json", help="read this json-v1 file instead of running ctest")
    args = parser.parse_args()
    if args.show_only_json:
        with open(args.show_only_json, encoding="utf-8") as stream:
            show = json.load(stream)
    else:
        completed = subprocess.run(
            [args.ctest, "--test-dir", args.build_dir, "--show-only=json-v1"],
            capture_output=True, text=True, timeout=120, check=False)
        if completed.returncode != 0:
            print(completed.stderr.strip() or "ctest --show-only failed", file=sys.stderr)
            return 1
        show = json.loads(completed.stdout)
    errors = check(show)
    for error in errors:
        print(f"[ctest-labels] ERROR: {error}", file=sys.stderr)
    if errors:
        return 1
    counts = {label: 0 for label in sorted(KINDS | TAGS)}
    for test in show["tests"]:
        for label in labels_of(test):
            if label in counts:
                counts[label] += 1
    summary = " ".join(f"{label}={count}" for label, count in counts.items())
    print(f"[ctest-labels] tests={len(show['tests'])} {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
