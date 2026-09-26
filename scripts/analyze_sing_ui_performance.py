#!/usr/bin/env python3
"""Read an existing SING fidelity packet and summarize its measured paint timings.

Usage: python3 scripts/analyze_sing_ui_performance.py build/evidence/ui-fidelity/a4630958

This does not run the app or derive new percentiles: the packet has per-run quantiles, not raw
frame samples. A paint-only result is not an end-to-end frame-time or host-performance verdict.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys
from typing import Any


SCHEMA = "seam-ui-fidelity-performance-v1"
CAPTURE_SCHEMA = "seam-ui-performance-evidence-v1"
REPORT_SCHEMA = "seam-ui-performance-analysis-v1"
PAINT_SOURCE = "NativeEditorApp::paint wall time for every frame of this run"
LIMITATIONS = [
    "The packet stores p50/p95/max, not raw samples; capture quantiles cannot be pooled into a "
    "cross-run percentile or assigned confidence intervals.",
    "NativeEditorApp::paint wall time includes controller work inside that callback without "
    "separating it. It excludes work before callback entry, CGImage/AppKit presentation, "
    "host display timing, and native accessibility invalidation; a paint-budget result "
    "is not an end-to-end frame-time result.",
    "No per-phase timings or call graph are present. Grid/notes, rack, stage, glow, text, "
    "composition, and presenter costs cannot be attributed from this packet.",
    "The captures use fixed states and viewports. They do not measure scrolling, dragging, "
    "steady playback, mode switching, FL Studio, or the specified 10,000-note workload.",
    "Captures with fewer than 100 paint samples are flagged as short runs; their p95 is "
    "especially sensitive to a small number of frames.",
    "physFootprintBytes is whole-process memory, not retained UI raster/cache allocation.",
]


class EvidenceError(ValueError):
    """The packet does not support a trustworthy analysis."""


def _read_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EvidenceError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise EvidenceError(f"{path}: expected a JSON object")
    return value


def _number(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise EvidenceError(f"{label}: expected a finite number")
    result = float(value)
    if result < 0.0:
        raise EvidenceError(f"{label}: expected a nonnegative number")
    return result


def _viewport(value: Any, label: str) -> list[int]:
    if (not isinstance(value, list) or len(value) != 2 or
            any(isinstance(v, bool) or not isinstance(v, int) or v <= 0 for v in value)):
        raise EvidenceError(f"{label}: expected two positive integer dimensions")
    return value


def analyze(packet: Path, budget_ms: float = 16.7) -> dict[str, Any]:
    """Analyze exact captures listed by the same packet's manifest.

    A missing capture or unknown timing schema fails closed. This prevents an incomplete packet
    from looking like a green performance result after a failed capture is omitted.
    """
    budget_ms = _number(budget_ms, "budget_ms")
    if budget_ms == 0:
        raise EvidenceError("budget_ms must be greater than zero")
    folder = packet if packet.is_dir() else packet.parent
    performance = _read_object(folder / "performance.json")
    manifest = _read_object(folder / "manifest.json")
    if performance.get("schema") != SCHEMA:
        raise EvidenceError(f"performance.json: unsupported schema {performance.get('schema')!r}")
    raw_captures = performance.get("captures")
    listed_captures = manifest.get("captures")
    if not isinstance(raw_captures, dict) or not isinstance(listed_captures, list):
        raise EvidenceError("packet needs performance captures and manifest captures")
    scale = _number(manifest.get("deviceScale"), "manifest.deviceScale")
    if scale == 0:
        raise EvidenceError("manifest.deviceScale must be greater than zero")

    rows = []
    seen = set()
    for listed in listed_captures:
        if not isinstance(listed, dict):
            raise EvidenceError("manifest capture must be an object")
        capture_id = listed.get("id")
        mode = listed.get("mode")
        state = listed.get("state")
        if (not isinstance(capture_id, str) or not capture_id or capture_id in seen or
                mode not in ("emo", "scene") or not isinstance(state, str) or not state):
            raise EvidenceError(f"invalid or duplicate capture identity: {capture_id!r}")
        seen.add(capture_id)
        viewport = _viewport(listed.get("viewport"), f"{capture_id}.viewport")
        item = raw_captures.get(capture_id)
        if not isinstance(item, dict) or item.get("schema") != CAPTURE_SCHEMA:
            raise EvidenceError(f"{capture_id}: missing or unsupported performance capture")
        if item.get("source") != PAINT_SOURCE:
            raise EvidenceError(f"{capture_id}: unexpected timing source {item.get('source')!r}")
        frames = item.get("frames")
        if isinstance(frames, bool) or not isinstance(frames, int) or frames <= 0:
            raise EvidenceError(f"{capture_id}.frames: expected a positive integer")
        paint = item.get("paintMillis")
        if not isinstance(paint, dict):
            raise EvidenceError(f"{capture_id}: paintMillis is missing")
        p50 = _number(paint.get("p50"), f"{capture_id}.p50")
        p95 = _number(paint.get("p95"), f"{capture_id}.p95")
        maximum = _number(paint.get("max"), f"{capture_id}.max")
        if p50 > p95 or p95 > maximum:
            raise EvidenceError(f"{capture_id}: expected p50 <= p95 <= max")
        memory = item.get("memory")
        if not isinstance(memory, dict) or memory.get("scope") != "whole standalone process (phys_footprint)":
            raise EvidenceError(f"{capture_id}: unexpected memory scope")
        footprint = _number(memory.get("physFootprintBytes"), f"{capture_id}.physFootprintBytes")
        peak = _number(memory.get("physFootprintPeakBytes"), f"{capture_id}.physFootprintPeakBytes")
        if peak < footprint:
            raise EvidenceError(f"{capture_id}: footprint peak is below final footprint")
        rows.append({
            "id": capture_id, "mode": mode, "state": state, "viewport": viewport,
            "deviceScale": scale, "frames": frames, "paintP50Ms": p50, "paintP95Ms": p95,
            "paintMaxMs": maximum, "p95OverBudgetMs": round(max(0.0, p95 - budget_ms), 3),
            "paintBudget": "OVER_BUDGET" if p95 > budget_ms else "WITHIN_BUDGET",
            "shortRun": frames < 100,
            "processFootprintMiB": round(footprint / 1048576, 1),
            "processPeakMiB": round(peak / 1048576, 1),
        })
    extra = set(raw_captures) - seen
    if extra:
        raise EvidenceError(f"performance captures absent from manifest: {', '.join(sorted(extra))}")
    if not rows:
        raise EvidenceError("packet has no captures")
    rows.sort(key=lambda row: (row["viewport"][0], row["viewport"][1], row["state"], row["mode"]))
    return {
        "schema": REPORT_SCHEMA, "candidate": manifest.get("candidate", "unknown"),
        "budgetMs": budget_ms, "timingScope": PAINT_SOURCE, "rows": rows,
        "overBudgetCount": sum(row["paintBudget"] == "OVER_BUDGET" for row in rows),
        "captureCount": len(rows), "shortRunCount": sum(row["shortRun"] for row in rows),
        "endToEndVerdict": "NOT_MEASURED",
        "phaseTimings": "NOT_MEASURED", "limitations": LIMITATIONS,
    }


def markdown(report: dict[str, Any]) -> str:
    lines = [
        f"# SING paint timing: {report['candidate']}", "",
        f"Paint-only p95 budget: {report['budgetMs']:g} ms. "
        f"Over budget: {report['overBudgetCount']}/{report['captureCount']} captures. "
        f"Short runs (<100 frames): {report['shortRunCount']}. "
        "End-to-end verdict: NOT_MEASURED.", "",
        "| Viewport @ scale | State | Mode | Frames | Paint p50 / p95 / max (ms) | p95 vs budget | Process peak (MiB) |",
        "|---|---|---|---:|---:|---|---:|",
    ]
    for row in report["rows"]:
        width, height = row["viewport"]
        difference = row["paintP95Ms"] - report["budgetMs"]
        comparison = (f"+{difference:.1f} ms" if difference > 0 else
                      f"{abs(difference):.1f} ms headroom")
        lines.append(
            f"| {width}×{height} @{row['deviceScale']:g}× | {row['state']} | {row['mode']} | "
            f"{row['frames']}{'*' if row['shortRun'] else ''} | "
            f"{row['paintP50Ms']:.1f} / {row['paintP95Ms']:.1f} / "
            f"{row['paintMaxMs']:.1f} | {row['paintBudget']} "
            f"({comparison}) | {row['processPeakMiB']:.1f} |"
        )
    if report["shortRunCount"]:
        lines += ["", "* Short run: fewer than 100 measured paint frames."]
    lines += ["", "## Measurement limits", ""]
    lines.extend(f"- {limit}" for limit in report["limitations"])
    lines += ["", "Next instrumentation seam: time read-model/layout, grid and notes, rack/stage, "
              "text/glow, image composition, and AppKit presentation separately in the actual "
              "native paint path; record interaction scenarios and raw frame samples.", ""]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packet", type=Path, help="packet directory or its performance.json")
    parser.add_argument("--budget-ms", type=float, default=16.7)
    parser.add_argument("--format", choices=("markdown", "json"), default="markdown")
    args = parser.parse_args(argv)
    try:
        report = analyze(args.packet, args.budget_ms)
    except EvidenceError as error:
        parser.exit(2, f"performance evidence error: {error}\n")
    print(json.dumps(report, indent=2) if args.format == "json" else markdown(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
