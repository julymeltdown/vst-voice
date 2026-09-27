#!/usr/bin/env python3
"""Compare two SING UI-fidelity packets for reproducibility (redesign plan section 14.4).

The plan asks for the same build to be captured twice and to hash identically. The packet's PNG
files carry a creation-time metadata chunk, so this compares the pixel hashes the capture script
records for every frame (`softwarePixelSha256`) and the configuration both runs were made with.

  python3 scripts/compare_fidelity_packets.py <packet-a> <packet-b> [--require-identical]

Exit status 0 when every frame matches, 1 otherwise. `--require-identical` also fails when the two
packets were not captured from the same source and binary, which is what a reproducibility run
must hold.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys


def load(packet: pathlib.Path) -> dict:
    manifest = packet / "manifest.json"
    if not manifest.is_file():
        raise SystemExit(f"no manifest.json in {packet}")
    return json.loads(manifest.read_text(encoding="utf-8"))


def flat(manifest: dict) -> dict[str, str]:
    records = manifest.get("captures") or {}
    frames = records.values() if isinstance(records, dict) else records
    out: dict[str, str] = {}
    for record in frames:
        digest = record.get("softwarePixelSha256")
        if digest:
            out[record["id"]] = digest
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("a", type=pathlib.Path)
    parser.add_argument("b", type=pathlib.Path)
    parser.add_argument("--require-identical", action="store_true",
                        help="also fail when source or binary differ")
    args = parser.parse_args()

    left, right = load(args.a), load(args.b)
    failures: list[str] = []
    for what, read in (("candidate", lambda m: m.get("candidate")),
                       ("binary", lambda m: (m.get("binary") or {}).get("sha256"))):
        a, b = read(left), read(right)
        if a != b:
            message = f"different {what}: {a} vs {b}"
            (failures if args.require_identical else []).append(message)
            print(f"note: {message}")

    fa, fb = flat(left), flat(right)
    only_a = sorted(set(fa) - set(fb))
    only_b = sorted(set(fb) - set(fa))
    if only_a:
        failures.append(f"only in A: {', '.join(only_a)}")
    if only_b:
        failures.append(f"only in B: {', '.join(only_b)}")
    differing = sorted(k for k in set(fa) & set(fb) if fa[k] != fb[k])
    for name in differing:
        failures.append(f"pixels differ: {name}")

    print(f"frames compared: {len(set(fa) & set(fb))}, identical: {len(set(fa) & set(fb)) - len(differing)}")
    if failures:
        for line in failures:
            print(f"FAIL {line}")
        return 1
    print("PASS every frame's pixels match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
