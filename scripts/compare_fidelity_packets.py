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
import hashlib
import json
import pathlib
import re
import sys


def load(packet: pathlib.Path) -> dict:
    manifest = packet / "manifest.json"
    if not manifest.is_file():
        raise SystemExit(f"no manifest.json in {packet}")
    return json.loads(manifest.read_text(encoding="utf-8"))


def frames(manifest: dict, packet: pathlib.Path) -> dict[str, dict]:
    """Fail closed on lost frames and verify the stored hashes against the actual PNG pixels."""
    from PIL import Image

    if manifest.get("result") != "PASS":
        raise ValueError("packet-level checks are missing or failed")
    records = manifest.get("captures") or []
    frames = records.values() if isinstance(records, dict) else records
    out: dict[str, dict] = {}
    for record in frames:
        name = record.get("id")
        if not name or name in out:
            raise ValueError(f"missing or duplicate capture id: {name}")
        if record.get("error") or record.get("exitCode") != 0 or record.get("stateReached") is not True:
            raise ValueError(f"{name}: failed capture or requested state not reached")
        for check in ("geometryCheck", "semanticCheck", "imageCheck"):
            if (record.get(check) or {}).get("result") != "PASS":
                raise ValueError(f"{name}: {check} is missing or failed")
        digest = record.get("softwarePixelSha256")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"{name}: missing or invalid pixel hash")
        image_path = (packet / record.get("softwarePng", "")).resolve()
        if not image_path.is_relative_to(packet.resolve()) or not image_path.is_file():
            raise ValueError(f"{name}: missing PNG inside the packet")
        with Image.open(image_path) as image:
            if list(image.size) != record.get("softwarePixels"):
                raise ValueError(f"{name}: PNG dimensions differ from the manifest")
            actual = hashlib.sha256(image.convert("RGBA").tobytes()).hexdigest()
        if actual != digest:
            raise ValueError(f"{name}: PNG pixels differ from the declared hash")
        out[name] = record
    if not out:
        raise ValueError("packet contains no frames")
    count = (manifest.get("requestedMatrix") or {}).get("captureCount")
    if count is not None and count != len(out):
        raise ValueError(f"{len(out)} frames for a requested {count}")
    return out


def plan_cells(records: list[dict]) -> dict:
    # Independent of a packet's self-declared count: these are plan section 14.4's 160 cells.
    expected = {(mode, state, size, contrast, scale)
                for mode in ("emo", "scene")
                for state in ("empty", "dense-overlap", "selection", "rendering", "failed")
                for size in ((1600, 900), (1100, 720), (860, 640), (720, 480))
                for contrast in ("standard", "high") for scale in (1, 2)}
    actual = {(r.get("mode"), r.get("state"), tuple(r.get("viewport") or []),
               r.get("contrast"), r.get("requestedDeviceScale")) for r in records}
    return {"required": len(expected), "covered": len(expected & actual),
            "complete": expected <= actual, "missing": sorted(expected - actual)}


def independent_runs(left: dict, right: dict, a: pathlib.Path, b: pathlib.Path) -> bool:
    return (a.resolve() != b.resolve() and bool(left.get("captureRunId")) and
            bool(right.get("captureRunId")) and left["captureRunId"] != right["captureRunId"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("a", type=pathlib.Path)
    parser.add_argument("b", type=pathlib.Path)
    parser.add_argument("--require-identical", action="store_true",
                        help="also fail when source or binary differ")
    parser.add_argument("--output", type=pathlib.Path, help="write the comparison result as JSON")
    parser.add_argument("--require-plan-matrix", action="store_true",
                        help="also require all 160 plan section 14.4 score cells in each run")
    args = parser.parse_args()

    left, right = load(args.a), load(args.b)
    failures: list[str] = []
    if not independent_runs(left, right, args.a, args.b):
        failures.append("two distinct v2 capture runs are required")
    for what, read in (("candidate", lambda m: m.get("candidate")),
                       ("binary", lambda m: (m.get("binary") or {}).get("sha256"))):
        a, b = read(left), read(right)
        if not a or not b or a != b:
            message = f"different {what}: {a} vs {b}"
            (failures if args.require_identical else []).append(message)
            print(f"note: {message}")

    if args.require_identical:
        for key in ("source", "fonts", "assets", "fixture", "contract", "requestedMatrix", "voicebank", "environment"):
            if not left.get(key) or not right.get(key) or left[key] != right[key]:
                failures.append(f"different {key} configuration")
        for label, manifest in (("A", left), ("B", right)):
            if (manifest.get("build") or {}).get("builtBeforeCapture") is not True:
                failures.append(f"{label}: binary was not built from the captured source")
    try:
        fa, fb = frames(left, args.a), frames(right, args.b)
    except (ValueError, OSError) as error:
        print(f"FAIL {error}")
        return 1
    only_a = sorted(set(fa) - set(fb))
    only_b = sorted(set(fb) - set(fa))
    if only_a:
        failures.append(f"only in A: {', '.join(only_a)}")
    if only_b:
        failures.append(f"only in B: {', '.join(only_b)}")
    common = set(fa) & set(fb)
    differing = sorted(k for k in common if fa[k]["softwarePixelSha256"] != fb[k]["softwarePixelSha256"])
    for name in sorted(common):
        for key in ("mode", "state", "viewport", "contrast", "requestedDeviceScale", "softwarePixels"):
            if fa[name].get(key) != fb[name].get(key):
                failures.append(f"{name}: different {key}")
    for name in differing:
        failures.append(f"pixels differ: {name}")

    coverage = {"A": plan_cells(list(fa.values())), "B": plan_cells(list(fb.values()))}
    for label, scope in coverage.items():
        print(f"{label} plan score matrix: {scope['covered']}/{scope['required']} cells")
        if args.require_plan_matrix and not scope["complete"]:
            failures.append(f"{label}: incomplete plan score matrix")

    print(f"frames compared: {len(common)}, identical: {len(common) - len(differing)}")
    if args.output:
        args.output.write_text(json.dumps({"result": "FAIL" if failures else "PASS",
            "packets": [str(args.a), str(args.b)], "candidate": left.get("candidate"),
            "compared": len(common), "identical": len(common) - len(differing),
            "pixelFilesVerified": True, "planMatrixCoverage": coverage, "failures": failures}, indent=2) + "\n")
    if failures:
        for line in failures:
            print(f"FAIL {line}")
        return 1
    print("PASS every validated frame's pixels match within the captured scope")
    return 0


if __name__ == "__main__":
    sys.exit(main())
