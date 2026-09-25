#!/usr/bin/env python3
"""Check the approved UI specification; never claim native visual acceptance.

Standard library only. This is a document/reference consistency check, not a
screenshot comparator, renderer test, or FL Studio test.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONTRACT = ROOT / "docs/design/ui-fidelity-contract-v1.json"


def positive_number(value: object) -> bool:
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
        and value > 0
    )


def valid_rect(rect: object) -> bool:
    return (
        isinstance(rect, list)
        and len(rect) == 4
        and all(isinstance(x, (int, float)) and not isinstance(x, bool)
                and math.isfinite(x) for x in rect)
        and rect[2] > 0
        and rect[3] > 0
    )


def contains(parent: list, child: list) -> bool:
    return (parent[0] <= child[0] and parent[1] <= child[1]
            and child[0] + child[2] <= parent[0] + parent[2]
            and child[1] + child[3] <= parent[1] + parent[3])


def overlaps(a: list, b: list) -> bool:
    return (max(a[0], b[0]) < min(a[0] + a[2], b[0] + b[2])
            and max(a[1], b[1]) < min(a[1] + a[3], b[1] + b[3]))


def validate(data: dict, root: Path = ROOT) -> dict:
    errors: list[str] = []
    if data["schemaVersion"] != 1 or data["status"] != "specification-only":
        errors.append("Unsupported contract schema/status")
    if data["nativeVisualMatch"] != "NOT_RUN":
        errors.append("A specification check cannot certify native visual matching")
    if data["captureRequirements"]["runtimeEvidencePresent"] is not False:
        errors.append("Native evidence needs a separate runtime verification record")

    references = data["references"]
    if {item["id"] for item in references} != {"emo", "scene", "protagonist"} or len(references) != 3:
        errors.append("Expected one EMO, SCENE and protagonist reference")
    for item in references:
        path = (root / item["path"]).resolve()
        if not path.is_relative_to(root.resolve()):
            errors.append(f"Reference escapes repository: {item['id']}")
            continue
        if not path.is_file() or path.stat().st_size > 64 * 1024 * 1024:
            errors.append(f"Reference missing or too large: {item['id']}")
            continue
        image = path.read_bytes()
        if hashlib.sha256(image).hexdigest() != item["sha256"]:
            errors.append(f"Reference hash mismatch: {item['id']}")
        if len(image) < 24 or image[:8] != b"\x89PNG\r\n\x1a\n" or image[12:16] != b"IHDR":
            errors.append(f"Invalid PNG header: {item['id']}")
            continue
        size = list(struct.unpack(">II", image[16:24]))
        if size != item["size"]:
            errors.append(f"Reference dimensions changed: {item['id']}")
        crop = item.get("approximateFrameCrop")
        if crop is not None and (not valid_rect(crop) or not contains([0, 0, *size], crop)):
            errors.append(f"Frame crop outside reference: {item['id']}")

    canonical = data["canonical"]
    expected_regions = {
        "header", "wordmark", "editor", "tools", "ruler", "keyboard", "grid",
        "lane", "laneTabs", "lanePlot", "rack", "singer", "portraitRing",
        "expression", "style", "status",
        "workspaceTabs", "modeSwitch", "transport", "outputMeter", "settings",
        "laneTimePlot",
    }
    if {region["id"] for region in canonical["regions"]} != expected_regions:
        errors.append("Canonical region inventory is incomplete or changed without a schema revision")
    if set(data["typography"]) != {
        "body", "label", "lyric", "smallLabel", "rulerMicro", "panelTitle", "knobValue", "transport"
    }:
        errors.append("Typography role inventory is incomplete")
    width, height = canonical["logicalSize"]
    if not positive_number(width) or not positive_number(height):
        raise ValueError("Canonical size must contain positive finite dimensions")
    rects = {"client": [0, 0, width, height]}
    parents = {}
    for region in canonical["regions"]:
        name, rect = region["id"], region["rect"]
        if name in rects or not valid_rect(rect):
            errors.append(f"Duplicate id or invalid rectangle: {name}")
            continue
        rects[name] = rect
        parents[name] = region["parent"]
    for name, parent in parents.items():
        if parent not in rects or not contains(rects[parent], rects[name]):
            errors.append(f"Region outside its parent: {name}")
        seen = {name}
        ancestor = parent
        while ancestor != "client":
            if ancestor in seen or ancestor not in parents:
                errors.append(f"Cyclic/missing parent for region: {name}")
                break
            seen.add(ancestor)
            ancestor = parents[ancestor]
    required_groups = [
        ["header", "editor", "lane", "rack", "status"],
        ["wordmark", "workspaceTabs", "modeSwitch", "transport", "outputMeter", "settings"],
        ["tools", "ruler", "keyboard", "grid"],
        ["laneTabs", "lanePlot"],
        ["singer", "expression", "style"],
    ]
    def pairs(groups: list) -> set:
        return {tuple(sorted(pair)) for group in groups for pair in itertools.combinations(group, 2)}
    if not pairs(required_groups).issubset(pairs(canonical["nonOverlappingGroups"])):
        errors.append("Required sibling-overlap checks are missing")
    for group in canonical["nonOverlappingGroups"]:
        for a, b in itertools.combinations(group, 2):
            if a not in rects or b not in rects or overlaps(rects[a], rects[b]):
                errors.append(f"Missing or overlapping sibling regions: {a}, {b}")
    time_axes = canonical["sharedTimeAxis"]
    if len(time_axes) != 3 or set(time_axes) != {"ruler", "grid", "laneTimePlot"}:
        errors.append("Shared musical time-axis checks are missing")
    elif any(name not in rects for name in time_axes):
        errors.append("Shared musical time-axis rectangle is missing")
    elif len({(rects[name][0], rects[name][2]) for name in time_axes}) != 1:
        errors.append("Ruler, grid and lane musical time axes are misaligned")

    for name, role in data["typography"].items():
        points, line_box = role["points"], role["lineBox"]
        if not positive_number(points) or not positive_number(line_box):
            errors.append(f"Nonpositive typography metric: {name}")
        elif points < (11 if role["essential"] else 10) or line_box < points * 1.2:
            errors.append(f"Text floor/line-box violation: {name}")
    physical_sizes = []
    if len(canonical["deviceScales"]) != 2 or set(canonical["deviceScales"]) != {1, 2}:
        errors.append("Schema v1 requires both backing scales 1 and 2")
    for scale in canonical["deviceScales"]:
        if not positive_number(scale):
            errors.append("Invalid backing scale")
            continue
        pw, ph = math.ceil(width * scale), math.ceil(height * scale)
        if abs(pw / scale - width) > 1 / scale or abs(ph / scale - height) > 1 / scale:
            errors.append("Logical-to-physical round-trip exceeds one physical pixel")
        physical_sizes.append([pw, ph])

    budget = data["rasterBudget"]
    if any(not positive_number(value) for value in budget.values()):
        raise ValueError("Raster budget values must be positive finite numbers")
    scale = budget["deviceScale"]
    one_buffer = math.ceil(width * scale) * math.ceil(height * scale) * budget["bytesPerPixel"]
    retained = (one_buffer * budget["fullSizeBuffers"] / 1024**2
                + sum(budget[key] for key in ("tileMiB", "scratchMiB", "activeArtMiB", "textIconsMiB")))
    if retained > budget["retainedLimitMiB"]:
        errors.append("Canonical retained raster allocations exceed the declared budget")

    return {
        "specification_check": "FAIL" if errors else "PASS",
        "native_visual_match": "NOT_RUN",
        "host_verification": "NOT_RUN",
        "scope": "reference integrity, canonical geometry, typography and allocation arithmetic only",
        "references_checked": len(references),
        "regions_checked": len(rects) - 1,
        "physical_client_sizes": physical_sizes,
        "planned_retained_raster_mib": round(retained, 3),
        "retained_limit_mib": budget["retainedLimitMiB"],
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    args = parser.parse_args()
    try:
        data = json.loads(args.contract.read_text(encoding="utf-8"))
        result = validate(data)
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as error:
        result = {"specification_check": "FAIL", "native_visual_match": "NOT_RUN",
                  "errors": [str(error)]}
    print(json.dumps(result, indent=2))
    return 0 if result["specification_check"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
