#!/usr/bin/env python3
"""Capture the SING UI-fidelity comparison packet.

Implements the packet of docs/design/SEAM_UI_FIDELITY_REVIEW_2026-09-25.md section 11.2 for the
standalone app on macOS:

  build/evidence/ui-fidelity/<candidate>/
    manifest.json         source (HEAD + dirty identity), binary, macOS, backend, scale, font,
                          asset, fixture, voicebank and contract hashes
    <mode>-software.png   exact client render of the canonical state (sRGB)
    <mode>-appkit.png     the OS-composited window of the same run, title bar removed, sRGB
    geometry.json         layout snapshot of every capture, checked against the contract
    semantic-bounds.json  published accessibility bounds of every capture, checked against layout
    roi-comparison.json   software vs AppKit per region (header, portrait, notes, expression,
                          lane, footer)
    performance.json      paint timings and process memory of every capture
    acceptance.md         results, deviations and what stays NOT_RUN (FL Studio, VoiceOver,
                          reviewer verdict, owner result)
    captures/             every capture of the matrix (states x modes x viewports)

Each capture launches the real app with --evidence-dir, so geometry and semantics come from the
snapshot that painted the frame. Nothing here certifies native visual acceptance: the packet
records measurements; the verdicts that need a person stay NOT_RUN.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
CONTRACT = ROOT / "docs/design/ui-fidelity-contract-v1.json"
DEFAULT_APP = ROOT / "build/release/Project SEAM.app/Contents/MacOS/Project SEAM"
DEFAULT_BANK = ROOT / "assets/demo-human-voicebank-public-domain/production-bank"
DEFAULT_FIXTURE = ROOT / "tests/fixtures/ui-fidelity/sing-phrase.seam"
DESIGN_ASSETS = ROOT / "assets/ui-design"
SYSTEM_FONTS = (
    Path("/System/Library/Fonts/SFNS.ttf"),
    Path("/System/Library/Fonts/SFNSMono.ttf"),
    Path("/System/Library/Fonts/Avenir Next Condensed.ttc"),
)
MODES = ("emo", "scene")
CANONICAL_VIEWPORT = (1600, 900)
# ROI name -> geometry region, per section 11.2 ("header, portrait, notes, expression, lane,
# footer separately").
ROIS = {
    "header": "header",
    "portrait": "portraitRing",
    "notes": "grid",
    "expression": "expression",
    "lane": "lane",
    "footer": "status",
}
# Initial regression targets of section 11.1; uncalibrated until a native golden exists.
FLAT_MAX_DELTA = 2
OUTLIER_DELTA = 8
OUTLIER_FRACTION = 0.005
# Semantic node -> the layout region its bounds must equal.
SEMANTIC_REGIONS = {
    "shell.status": "status",
    "shell.lane": "laneTimePlot",
    "shell.settings": "settings",
    "shell.style": "style",
}
KNOBS = ("formant", "breath", "tension", "air", "gender", "growl")
STALE_REASON = (
    "stale audio exists only after an edit follows a published render; the standalone command "
    "line cannot script an edit, so this state is covered by the shell tests, not by a capture"
)


class PacketError(RuntimeError):
    pass


# ---------------------------------------------------------------------------------------------
# Identity


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_text(command: list[str], cwd: Path = ROOT) -> str:
    completed = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
    return completed.stdout.strip() if completed.returncode == 0 else "unavailable"


def source_identity() -> dict[str, Any]:
    head = run_text(["git", "rev-parse", "HEAD"])
    if len(head) != 40:
        raise PacketError("cannot resolve the source commit")
    status = subprocess.run(
        ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"],
        cwd=ROOT, capture_output=True, check=True,
    ).stdout
    entries = [entry for entry in status.decode("utf-8", "replace").split("\0") if entry]
    paths = sorted({entry[3:] for entry in entries})
    digest = hashlib.sha256()
    digest.update(subprocess.run(["git", "diff", "HEAD", "--binary"], cwd=ROOT,
                                 capture_output=True, check=True).stdout)
    for entry in entries:
        if entry.startswith("??"):
            path = ROOT / entry[3:]
            if path.is_file():
                digest.update(entry[3:].encode() + b"\0" + path.read_bytes())
    dirty = bool(paths)
    return {
        "head": head,
        "dirty": dirty,
        "dirtyPaths": paths,
        "dirtyContentSha256": digest.hexdigest() if dirty else None,
        "candidate": head[:8] + (f"-dirty-{digest.hexdigest()[:8]}" if dirty else ""),
    }


def hash_tree(root: Path) -> dict[str, str]:
    return {
        str(path.relative_to(ROOT)): sha256_file(path)
        for path in sorted(root.rglob("*"))
        if path.is_file() and path.name != ".DS_Store"
    }


def environment_identity() -> dict[str, Any]:
    return {
        "macOS": run_text(["sw_vers", "-productVersion"]),
        "macOSBuild": run_text(["sw_vers", "-buildVersion"]),
        "machine": platform.machine(),
        "model": run_text(["sysctl", "-n", "hw.model"]),
    }


# ---------------------------------------------------------------------------------------------
# Fixtures


def region_of(project: dict[str, Any]) -> dict[str, Any]:
    try:
        return project["vocalTracks"][0]["regions"][0]
    except (KeyError, IndexError) as error:
        raise PacketError("the fixture has no first vocal region") from error


def derive_fixture(base: dict[str, Any], state: str) -> dict[str, Any]:
    project = copy.deepcopy(base)
    region = region_of(project)
    if state == "empty":
        region["notes"] = []
        region["lyrics"] = []
    elif state == "failed":
        # A real render failure, not a missing bank: the demo bank cannot sing ra, so the render
        # fails with "Voicebank cannot cover the phoneme sequence" and the shell must say why.
        region["lyrics"][2]["surface"] = "\u3089"
    elif state == "rendering":
        # A 600-note song keeps the render running for about a second and a half after launch,
        # long enough to close the app while it is still rendering (see --rendering-close-ms).
        notes, lyrics = [], []
        for repeat in range(100):
            for index, (note, lyric) in enumerate(zip(region["notes"], region["lyrics"])):
                serial = repeat * len(region["notes"]) + index
                copy_lyric = copy.deepcopy(lyric)
                copy_lyric["id"] = format(0x10000 + serial, "x")
                copy_note = copy.deepcopy(note)
                copy_note.update(id=format(0x20000 + serial, "x"), lyricId=copy_lyric["id"],
                                 startTick=note["startTick"] + repeat * 6 * 960)
                notes.append(copy_note)
                lyrics.append(copy_lyric)
        region["notes"], region["lyrics"] = notes, lyrics
        region["durationTick"] = 960 * (6 * 100 + 4)
    elif state == "dense-overlap":
        # Three stacked notes on one key and notes that overlap their neighbours in time: the
        # shell must show overlap bands, not paint notes over each other.
        notes, lyrics = region["notes"], region["lyrics"]
        template_note, template_lyric = notes[1], lyrics[1]
        extra = [(1920, 72), (1920, 72), (2400, 74), (3360, 72), (3360, 71)]
        for index, (start, key) in enumerate(extra):
            lyric = copy.deepcopy(template_lyric)
            lyric["id"] = format(0x7000 + index, "x")
            note = copy.deepcopy(template_note)
            note.update(id=format(0x7100 + index, "x"), lyricId=lyric["id"], midiKey=key,
                        startTick=start, durationTick=960)
            lyrics.append(lyric)
            notes.append(note)
    return project


# ---------------------------------------------------------------------------------------------
# Images


def srgb_image(image: Any, source_icc: bytes | None) -> tuple[Any, str]:
    """Return an RGB image in sRGB and how it got there."""
    from PIL import Image, ImageCms

    if image.mode in ("RGBA", "LA"):
        # Window corners are transparent in an OS capture; composite them on black.
        background = Image.new("RGB", image.size, (0, 0, 0))
        background.paste(image, mask=image.getchannel("A"))
        image = background
    else:
        image = image.convert("RGB")
    if not source_icc:
        return image, "assumed sRGB (no embedded profile)"
    srgb = ImageCms.createProfile("sRGB")
    source = ImageCms.ImageCmsProfile(io.BytesIO(source_icc))
    name = ImageCms.getProfileDescription(source).strip()
    converted = ImageCms.profileToProfile(image, source, srgb, outputMode="RGB",
                                          renderingIntent=ImageCms.Intent.RELATIVE_COLORIMETRIC)
    return converted, f"converted from embedded profile '{name}' to sRGB (relative colorimetric)"


def save_srgb(image: Any, path: Path) -> None:
    from PIL import ImageCms

    icc = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
    image.save(path, "PNG", icc_profile=icc)


def roi_metrics(software: Any, appkit: Any, rect: list[float], scale: float) -> dict[str, Any]:
    import numpy as np

    x0, y0 = int(round(rect[0] * scale)), int(round(rect[1] * scale))
    x1, y1 = int(round((rect[0] + rect[2]) * scale)), int(round((rect[1] + rect[3]) * scale))
    a = np.asarray(software.crop((x0, y0, x1, y1)), dtype=np.int16)
    b = np.asarray(appkit.crop((x0, y0, x1, y1)), dtype=np.int16)
    delta = np.abs(a - b).max(axis=2)
    pixels = int(delta.size)
    if pixels == 0:
        return {"pixelRect": [x0, y0, 0, 0], "pixels": 0, "withinInitialTarget": False}
    over = int((delta > OUTLIER_DELTA).sum())
    return {
        "pixelRect": [x0, y0, x1 - x0, y1 - y0],
        "pixels": pixels,
        "maxChannelDelta": int(delta.max()),
        "meanChannelDelta": round(float(delta.mean()), 4),
        "fractionOverDelta2": round(float((delta > FLAT_MAX_DELTA).sum()) / pixels, 6),
        "fractionOverDelta8": round(over / pixels, 6),
        "withinInitialTarget": over / pixels <= OUTLIER_FRACTION,
    }


# ---------------------------------------------------------------------------------------------
# Checks


def overlaps(a: list[float], b: list[float]) -> bool:
    return (max(a[0], b[0]) < min(a[0] + a[2], b[0] + b[2])
            and max(a[1], b[1]) < min(a[1] + a[3], b[1] + b[3]))


def contains(outer: list[float], inner: list[float], slack: float = 0.5) -> bool:
    return (inner[0] >= outer[0] - slack and inner[1] >= outer[1] - slack
            and inner[0] + inner[2] <= outer[0] + outer[2] + slack
            and inner[1] + inner[3] <= outer[1] + outer[3] + slack)


def spec_rack_width(width: float) -> float:
    if width < 860:
        return 44.0
    if width < 1100:
        return 56.0
    return min(max(width * 0.275, 320.0), 440.0)


def check_geometry(geometry: dict[str, Any], contract: dict[str, Any]) -> dict[str, Any]:
    canonical = contract["canonical"]
    tolerance = float(canonical["regionTolerancePoints"])
    regions = geometry["regions"]
    width, height = geometry["logicalSize"]
    visible = {name: rect for name, rect in regions.items() if rect[2] > 0 and rect[3] > 0}
    client = [0.0, 0.0, float(width), float(height)]
    failures: list[str] = []
    deviations: dict[str, list[float]] = {}
    at_canonical = [width, height] == list(canonical["logicalSize"])

    if at_canonical:
        for region in canonical["regions"]:
            actual = regions.get(region["id"])
            if actual is None:
                failures.append(f"{region['id']}: missing")
                continue
            delta = [round(a - e, 3) for a, e in zip(actual, region["rect"])]
            # The tolerance governs edges: left, top, right, bottom.
            edges = [delta[0], delta[1], delta[0] + delta[2], delta[1] + delta[3]]
            if any(abs(edge) > tolerance for edge in edges):
                deviations[region["id"]] = edges
                failures.append(f"{region['id']}: edges off by {edges} pt")
    parents = {region["id"]: region["parent"] for region in canonical["regions"]}
    for name, parent in parents.items():
        if name in visible and (parent == "client" or parent in visible):
            outer = client if parent == "client" else visible[parent]
            if not contains(outer, visible[name]):
                failures.append(f"{name}: outside {parent}")
    for group in canonical["nonOverlappingGroups"]:
        present = [name for name in group if name in visible]
        for index, first in enumerate(present):
            for second in present[index + 1:]:
                if overlaps(visible[first], visible[second]):
                    failures.append(f"{first} overlaps {second}")
    axis = [regions[name] for name in canonical["sharedTimeAxis"] if name in regions]
    if len({(rect[0], rect[2]) for rect in axis}) != 1:
        failures.append("ruler, grid and lane plot do not share one time axis")
    rack = regions.get("rack", [0, 0, 0, 0])
    rack_width = round(float(rack[2]), 3)
    spec_width = round(spec_rack_width(float(width)), 3)
    return {
        "logicalSize": [width, height],
        "comparedToCanonical": at_canonical,
        "tolerancePoints": tolerance,
        "edgeDeviations": deviations,
        "rackPresentation": geometry.get("rack"),
        "rackWidth": rack_width,
        "specRackWidth": spec_width,
        # Section 3.4 is prose, not part of the contract's canonical regions: a mismatch is a
        # reported deviation for the reviewer, not a geometry failure.
        "rackWidthMatchesSpec": abs(rack_width - spec_width) <= 0.5,
        "failures": failures,
        "result": "PASS" if not failures else "FAIL",
    }


def check_semantics(semantic: dict[str, Any], geometry: dict[str, Any]) -> dict[str, Any]:
    width, height = geometry["logicalSize"]
    client = [0.0, 0.0, float(width), float(height)]
    failures: list[str] = []
    seen: set[str] = set()
    for node in semantic["nodes"]:
        if node["id"] in seen:
            failures.append(f"duplicate id {node['id']}")
        seen.add(node["id"])
        bounds = node["bounds"]
        if node["parent"] and bounds[2] > 0 and bounds[3] > 0 and not contains(client, bounds):
            failures.append(f"{node['id']}: bounds outside the client")
    by_id = {node["id"]: node for node in semantic["nodes"]}
    for node_id, region in SEMANTIC_REGIONS.items():
        node = by_id.get(node_id)
        if node is None:
            if node_id == "shell.style" and geometry.get("rack") != "full":
                continue
            failures.append(f"{node_id}: not published")
        elif node["bounds"] != geometry["regions"][region]:
            failures.append(f"{node_id}: bounds {node['bounds']} != layout {region}")
    for index, knob in enumerate(KNOBS):
        node = by_id.get(f"shell.knob.{knob}")
        rect = geometry["controls"].get(f"knob{index}")
        if node is not None and rect is not None and node["bounds"] != rect:
            failures.append(f"{node['id']}: bounds differ from knob{index}")
    return {
        "nodes": len(semantic["nodes"]),
        "virtualizedNotes": semantic["virtualizedNoteCount"],
        "focused": semantic["focused"],
        "failures": failures,
        "result": "PASS" if not failures else "FAIL",
    }


# ---------------------------------------------------------------------------------------------
# Capture


def parse_log(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if separator and key and " " not in key:
            values[key] = value
    return values


def capture(args: argparse.Namespace, work: Path, mode: str, state: str,
            viewport: tuple[int, int], project: Path) -> dict[str, Any]:
    name = f"{mode}-{state}-{viewport[0]}x{viewport[1]}"
    folder = work / name
    folder.mkdir(parents=True)
    evidence = folder / "evidence"
    support = folder / "support"
    support.mkdir()
    window_id = folder / "window-id"
    ppm = folder / "software.ppm"
    appkit_raw = folder / "appkit-raw.png"
    close_ms = args.rendering_close_ms if state == "rendering" else args.close_ms
    command = [
        str(args.app), "--development", "--voicebank-root", str(args.voicebank_root),
        "--auto-close-ms", str(close_ms), "--screenshot", str(ppm),
        "--window-width", str(viewport[0]), "--window-height", str(viewport[1]),
        "--application-support-root", str(support), "--force-threaded-audio",
        "--evidence-dir", str(evidence), "--window-id-file", str(window_id), str(project),
    ]
    environment = dict(os.environ, SEAM_UI_DESIGN=mode)
    environment.pop("SEAM_UI_WORKSPACE", None)
    started = time.monotonic()
    process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    appkit_note = "not captured"
    lead_ms = min(args.appkit_lead_ms, close_ms * 0.25)
    capture_at = started + (close_ms - lead_ms) / 1000.0
    while process.poll() is None and not window_id.exists():
        time.sleep(0.05)
    if process.poll() is None and window_id.exists() and not args.no_appkit:
        # The window server needs a moment after the window opens before it can be captured.
        capture_at = max(capture_at, time.monotonic() + 0.25)
        time.sleep(max(0.0, capture_at - time.monotonic()))
        wid = window_id.read_text().strip()
        shot = subprocess.run(["screencapture", "-x", "-o", "-l", wid, str(appkit_raw)],
                              capture_output=True, text=True, check=False)
        appkit_note = ("captured" if shot.returncode == 0 and appkit_raw.is_file()
                       else f"screencapture failed: {shot.stderr.strip() or shot.returncode}")
    try:
        stdout, stderr = process.communicate(timeout=close_ms / 1000.0 + 90.0)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
        raise PacketError(f"{name}: the app did not close")
    (folder / "app.log").write_text(stdout + ("\n# stderr\n" + stderr if stderr else ""))
    log = parse_log(stdout)
    record: dict[str, Any] = {
        "id": name, "mode": mode, "state": state, "viewport": list(viewport),
        "exitCode": process.returncode, "log": log, "appkit": appkit_note,
        "command": [os.path.relpath(part, ROOT) if part.startswith(str(ROOT)) else part
                    for part in command[1:]],
    }
    if process.returncode != 0 or not ppm.is_file() or not (evidence / "geometry.json").is_file():
        record["error"] = f"exit {process.returncode}; stderr: {stderr.strip()[-400:]}"
        return record
    record["observedRenderState"] = log.get("render_state", "unknown")
    expected = {"ready": "ready", "failed": "failed", "dense-overlap": "ready",
                "rendering": "rendering", "empty": None}[state]
    record["stateReached"] = expected is None or record["observedRenderState"] == expected
    for part in ("geometry", "semantic-bounds", "performance"):
        record[part] = json.loads((evidence / f"{part}.json").read_text())
    return record


def process_images(record: dict[str, Any], folder: Path, out: Path) -> None:
    from PIL import Image

    captures = out / "captures"
    software, _ = srgb_image(Image.open(folder / "software.ppm"), None)
    save_srgb(software, captures / f"{record['id']}-software.png")
    record["softwarePng"] = f"captures/{record['id']}-software.png"
    record["softwarePixels"] = list(software.size)
    raw = folder / "appkit-raw.png"
    if not raw.is_file():
        return
    appkit = Image.open(raw)
    appkit.load()
    appkit_srgb, color_note = srgb_image(appkit, appkit.info.get("icc_profile"))
    width, height = appkit_srgb.size
    title_bar = height - software.size[1]
    record["appkitColor"] = color_note
    record["appkitRawPixels"] = [width, height]
    record["appkitPng"] = f"captures/{record['id']}-appkit.png"
    if width != software.size[0] or title_bar < 0:
        record["appkitAlignment"] = "size mismatch; not compared"
        save_srgb(appkit_srgb, out / record["appkitPng"])
        return
    client = appkit_srgb.crop((0, title_bar, width, height))
    record["appkitTitleBarPixels"] = title_bar
    record["appkitAlignment"] = "client area = capture minus the top title bar"
    save_srgb(client, out / record["appkitPng"])
    scale = float(record["geometry"]["deviceScale"])
    regions = record["geometry"]["regions"]
    record["roi"] = {
        roi: roi_metrics(software, client, regions[region], scale)
        for roi, region in ROIS.items() if regions.get(region, [0, 0, 0, 0])[2] > 0
    }


# ---------------------------------------------------------------------------------------------
# Packet


def build_matrix(args: argparse.Namespace) -> list[tuple[str, str, tuple[int, int]]]:
    requirements = json.loads(CONTRACT.read_text())["captureRequirements"]
    states = [state for state in requirements["states"] if state != "stale"]
    if args.states:
        wanted = args.states.split(",")
        states = [state for state in states if state in wanted]
    matrix = [(mode, state, CANONICAL_VIEWPORT) for state in states for mode in MODES]
    if not args.canonical_only:
        for viewport in requirements["viewports"]:
            if tuple(viewport) != CANONICAL_VIEWPORT:
                matrix += [(mode, "ready", (viewport[0], viewport[1])) for mode in MODES]
    return matrix


def mode_parity(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Same state and viewport must have the same geometry in both modes (section 11.3)."""
    first: dict[tuple[str, tuple[int, ...]], dict[str, Any]] = {}
    results = []
    for record in records:
        if "geometry" not in record:
            continue
        key = (record["state"], tuple(record["viewport"]))
        other = first.setdefault(key, record)
        if other is record:
            continue
        same = (other["geometry"]["regions"] == record["geometry"]["regions"]
                and other["geometry"]["controls"] == record["geometry"]["controls"])
        results.append({"state": key[0], "viewport": list(key[1]),
                        "result": "PASS" if same else "FAIL"})
    return results


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def acceptance_markdown(manifest: dict[str, Any], records: list[dict[str, Any]],
                        parity: list[dict[str, Any]]) -> str:
    source = manifest["source"]
    environment = manifest["environment"]
    lines = [
        f"# SING UI-fidelity packet {manifest['candidate']}",
        "",
        f"Source {source['head']} (dirty: {source['dirty']}), binary sha256 "
        f"{manifest['binary']['sha256']}, macOS {environment['macOS']} "
        f"({environment['macOSBuild']}), backend: {manifest['backend']}, device scale "
        f"{manifest['deviceScale']}.",
        "",
        "Measurements only. Native visual acceptance, FL Studio, VoiceOver and the reviewer and "
        "owner verdicts are not decided by this tool.",
        "",
        "## Captures",
        "",
        "| Capture | Render state | State reached | Geometry | Semantics | AppKit | Paint p50/p95 ms | Footprint MB |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for record in records:
        if "error" in record:
            lines.append(f"| {record['id']} | - | - | ERROR | ERROR | {record['appkit']} | - | - |")
            continue
        perf = record["performance"]
        paint = perf["paintMillis"]
        memory = perf["memory"].get("physFootprintBytes") or 0
        lines.append(
            f"| {record['id']} | {record['observedRenderState']} | "
            f"{'yes' if record['stateReached'] else 'NO'} | {record['geometryCheck']['result']} | "
            f"{record['semanticCheck']['result']} | {record['appkit']} | "
            f"{paint['p50']:.1f}/{paint['p95']:.1f} | {memory / 1048576:.0f} |")
    for title, key in (("Geometry failures", "geometryCheck"), ("Semantic failures", "semanticCheck")):
        lines += ["", f"## {title}", ""]
        failing = [r for r in records if r.get(key, {}).get("failures")]
        lines += [f"- {r['id']}: {'; '.join(r[key]['failures'])}" for r in failing] or ["- none"]
    lines += ["", "## Rack width against the responsive rule (section 3.4)", "",
              "Section 3.4 asks for a 44-point drawer button below 860 points and a 56-point rail "
              "below 1100. A mismatch is reported here for the reviewer; it is not a contract "
              "geometry failure.", "",
              "| Capture | Presentation | Width | Spec width | Matches |", "|---|---|---|---|---|"]
    for record in records:
        check = record.get("geometryCheck")
        if check and record["state"] == "ready":
            lines.append(f"| {record['id']} | {check['rackPresentation']} | {check['rackWidth']:g} | "
                         f"{check['specRackWidth']:g} | "
                         f"{'yes' if check['rackWidthMatchesSpec'] else 'DEVIATION'} |")
    lines += ["", "## Mode parity (same state and viewport, EMO vs SCENE geometry)", ""]
    lines += [f"- {p['state']} {p['viewport'][0]}x{p['viewport'][1]}: {p['result']}" for p in parity]
    lines += ["", "## Software vs AppKit per region (pixels over channel delta 8, and max delta)", "",
              "The initial targets of section 11.1 are uncalibrated. The AppKit frame is taken "
              "shortly before the app closes and the software frame at close, so moving content "
              "(meters, progress) can differ legitimately.", "",
              "| Capture | " + " | ".join(ROIS) + " |", "|---|" + "---|" * len(ROIS)]
    for record in records:
        if "roi" in record:
            cells = []
            for roi in ROIS:
                metric = record["roi"].get(roi)
                cells.append("-" if not metric or not metric["pixels"] else
                             f"{metric['fractionOverDelta8'] * 100:.2f}% ({metric['maxChannelDelta']})")
            lines.append(f"| {record['id']} | " + " | ".join(cells) + " |")
    lines += [
        "", "## NOT_RUN", "",
        "- emo-flstudio.png, scene-flstudio.png: the embedded FL Studio capture is an owner "
        "session (see the FL test package).",
        f"- stale state: {STALE_REASON}.",
        "- VoiceOver and Accessibility Inspector walk of the same frames.",
        "- Concept-to-native comparison (section 11.1 item 1): a reviewer judgement.",
        "",
        "## Verdicts", "",
        "- Reviewer verdict: PENDING",
        "- Owner result: NOT_RUN",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture the SING UI-fidelity packet.")
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--voicebank-root", type=Path, default=DEFAULT_BANK)
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument("--output", type=Path, help="default build/evidence/ui-fidelity/<candidate>")
    parser.add_argument("--states", help="comma-separated subset of the contract states")
    parser.add_argument("--canonical-only", action="store_true",
                        help="only 1600x900 (skip the responsive ready captures)")
    parser.add_argument("--no-appkit", action="store_true", help="skip the OS window captures")
    parser.add_argument("--close-ms", type=int, default=10000)
    parser.add_argument("--rendering-close-ms", type=int, default=1300)
    parser.add_argument("--appkit-lead-ms", type=int, default=1200,
                        help="take the window capture this long before the app closes")
    args = parser.parse_args()
    args.app = args.app.resolve()
    args.voicebank_root = args.voicebank_root.resolve()
    args.fixture = args.fixture.resolve()

    if sys.platform != "darwin":
        raise PacketError("the packet captures the macOS AppKit window; run it on macOS")
    for required in (args.app, args.fixture, args.voicebank_root, CONTRACT):
        if not required.exists():
            raise PacketError(f"missing: {required}")
    source = source_identity()
    out = (args.output or ROOT / "build/evidence/ui-fidelity" / source["candidate"]).resolve()
    if out.exists() and any(out.iterdir()):
        raise PacketError(f"output must be empty: {out}")
    (out / "captures").mkdir(parents=True, exist_ok=True)
    contract = json.loads(CONTRACT.read_text())
    base = json.loads(args.fixture.read_text())

    work = Path(tempfile.mkdtemp(prefix="seam-ui-fidelity-"))
    fixtures: dict[str, Path] = {}
    fixture_hashes: dict[str, str] = {"base": sha256_file(args.fixture)}
    records: list[dict[str, Any]] = []
    try:
        for mode, state, viewport in build_matrix(args):
            if state not in fixtures:
                path = work / f"fixture-{state}.seam"
                path.write_text(json.dumps(derive_fixture(base, state), indent=2))
                fixtures[state] = path
                fixture_hashes[state] = sha256_file(path)
            project = work / f"{mode}-{state}-{viewport[0]}x{viewport[1]}.seam"
            shutil.copyfile(fixtures[state], project)
            print(f"capture {mode} {state} {viewport[0]}x{viewport[1]}", flush=True)
            record = capture(args, work, mode, state, viewport, project)
            if "error" not in record:
                record["geometryCheck"] = check_geometry(record["geometry"], contract)
                record["semanticCheck"] = check_semantics(record["semantic-bounds"], record["geometry"])
                process_images(record, work / record["id"], out)
            records.append(record)
    finally:
        # Work files go to the Trash, never deleted outright.
        shutil.move(str(work), str(Path.home() / ".Trash" / work.name))

    for record in records:
        if record["state"] == "ready" and tuple(record["viewport"]) == CANONICAL_VIEWPORT:
            for kind in ("software", "appkit"):
                if f"{kind}Png" in record:
                    shutil.copyfile(out / record[f"{kind}Png"], out / f"{record['mode']}-{kind}.png")
    parity = mode_parity(records)
    bank_in_repo = args.voicebank_root.is_relative_to(ROOT)
    manifest = {
        "schema": "seam-ui-fidelity-packet-v1",
        "candidate": source["candidate"],
        "source": source,
        "binary": {"path": os.path.relpath(args.app, ROOT), "sha256": sha256_file(args.app)},
        "environment": environment_identity(),
        "backend": next((r["log"].get("window_backend") for r in records if r.get("log")), "unknown"),
        "deviceScale": next((r["geometry"]["deviceScale"] for r in records if "geometry" in r), None),
        "uiZoom": 1.0,
        "fonts": {str(path): sha256_file(path) if path.is_file() else "unavailable"
                  for path in SYSTEM_FONTS},
        "assets": hash_tree(DESIGN_ASSETS),
        "fixture": {"path": os.path.relpath(args.fixture, ROOT), "sha256": fixture_hashes},
        "voicebank": {
            "root": os.path.relpath(args.voicebank_root, ROOT) if bank_in_repo else str(args.voicebank_root),
            "files": hash_tree(args.voicebank_root) if bank_in_repo else {},
        },
        "contract": {"path": os.path.relpath(CONTRACT, ROOT), "sha256": sha256_file(CONTRACT)},
        "captures": [
            {key: record[key] for key in (
                "id", "mode", "state", "viewport", "exitCode", "observedRenderState",
                "stateReached", "appkit", "appkitColor", "appkitTitleBarPixels", "appkitAlignment",
                "softwarePng", "appkitPng", "softwarePixels", "command", "error") if key in record}
            for record in records
        ],
        "notRun": {
            "flstudio": "owner session in FL Studio",
            "stale": STALE_REASON,
            "voiceover": "owner session",
        },
        "verdicts": {"reviewer": "PENDING", "owner": "NOT_RUN", "nativeVisualMatch": "NOT_RUN"},
    }
    write_json(out / "manifest.json", manifest)
    write_json(out / "geometry.json", {
        "schema": "seam-ui-fidelity-geometry-v1",
        "captures": {r["id"]: {"snapshot": r["geometry"], "check": r["geometryCheck"]}
                     for r in records if "geometry" in r},
        "modeParity": parity,
    })
    write_json(out / "semantic-bounds.json", {
        "schema": "seam-ui-fidelity-semantics-v1",
        "captures": {r["id"]: {"snapshot": r["semantic-bounds"], "check": r["semanticCheck"]}
                     for r in records if "semantic-bounds" in r},
    })
    write_json(out / "roi-comparison.json", {
        "schema": "seam-ui-fidelity-roi-v1",
        "method": ("software PPM vs OS window capture of the same run, both sRGB, title bar "
                   "removed; per-pixel max channel delta"),
        "initialTargets": {"flatMaxDelta": FLAT_MAX_DELTA, "outlierDelta": OUTLIER_DELTA,
                           "outlierFraction": OUTLIER_FRACTION, "calibrated": False},
        "regions": ROIS,
        "captures": {r["id"]: r["roi"] for r in records if "roi" in r},
    })
    write_json(out / "performance.json", {
        "schema": "seam-ui-fidelity-performance-v1",
        "captures": {r["id"]: r["performance"] for r in records if "performance" in r},
    })
    (out / "acceptance.md").write_text(acceptance_markdown(manifest, records, parity),
                                       encoding="utf-8")

    errors = [r["id"] for r in records if "error" in r]
    failed = [r["id"] for r in records
              if "error" not in r and ("FAIL" in (r["geometryCheck"]["result"], r["semanticCheck"]["result"])
                                       or not r["stateReached"])]
    failed += [f"parity {p['state']} {p['viewport']}" for p in parity if p["result"] == "FAIL"]
    print(out)
    print(f"captures={len(records)} errors={len(errors)} failed_or_unreached={len(failed)}")
    for item in errors + failed:
        print(f"  {item}")
    if errors:
        return 1
    return 2 if failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PacketError as error:
        print(f"ui-fidelity packet: {error}", file=sys.stderr)
        raise SystemExit(1)
