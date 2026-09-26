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
import re
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
# Capture-only state: the ready fixture with the compact singer inspector open.
INSPECTOR_STATE = "inspector"
# Capture-only states: the ready fixture with the TUNE or MIX workspace covering the score.
WORKSPACE_STATES = ("tune", "mix")
# Score nodes that must never be published under a covering workspace.
SCORE_IDS = ("timeline", "shell.waveform")
SCORE_PREFIXES = ("note.", "editor.vibrato.handle.", "overlap-group.", "detail.", "shell.lane")
MIX_STRIP_CONTROLS = ("gain", "pan", "mute", "solo", "route")
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
# Report columns: TUNE and MIX captures compare their body in place of the notes and lane.
ROI_COLUMNS = tuple(ROIS) + ("body",)
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


def build_app(app: Path, skip: bool) -> dict[str, Any]:
    """Builds the app's own build directory before capturing, so the binary is a build of the
    recorded source tree by construction (a no-op when it already is).

    HEAD alone does not say which tree a binary came from, and a ninja dry run cannot say either:
    its glob re-check always reports a CMake re-run and stops listing there."""
    build_dir = next((parent for parent in app.parents if (parent / "build.ninja").is_file()), None)
    relative = (None if build_dir is None else
                os.path.relpath(build_dir, ROOT) if build_dir.is_relative_to(ROOT) else str(build_dir))
    if skip or build_dir is None:
        return {"buildDir": relative, "builtBeforeCapture": False,
                "sourceBinding": "unverified (" + ("--no-build" if skip else "no build.ninja above the app") + ")"}
    completed = subprocess.run(["ninja", "-C", str(build_dir)], capture_output=True, text=True,
                               check=False)
    if completed.returncode != 0:
        raise PacketError(f"building {relative} failed:\n{completed.stdout[-2000:]}{completed.stderr[-1000:]}")
    steps = [line for line in completed.stdout.splitlines()
             if " Building " in line or " Linking " in line]
    return {"buildDir": relative, "builtBeforeCapture": True, "stepsPerformed": len(steps),
            "sourceBinding": "built from the recorded source tree immediately before capture"}


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


def positive(rect: Any) -> bool:
    return (isinstance(rect, list) and len(rect) == 4
            and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in rect)
            and rect[2] > 0 and rect[3] > 0)


def spec_rack_width(width: float) -> float:
    if width < 860:
        return 44.0
    if width < 1100:
        return 56.0
    return min(max(width * 0.275, 320.0), 440.0)


def spec_rack_presentation(width: float) -> str:
    """Section 3.4: a drawer button below 860 points, a rail below 1100, the full rack above."""
    if width < 860:
        return "drawer"
    if width < 1100:
        return "rail"
    return "full"


# Every check below fails closed: evidence that is missing, empty or for another viewport, mode or
# presentation is a failure, never a pass. The required sets follow what each presentation must
# expose; regions that section 3.4 lets collapse are required only at the canonical size.
ALWAYS_REGIONS = (
    "header", "transport", "settings", "editor", "tools", "ruler",
    "keyboard", "grid", "lane", "laneTabs", "lanePlot", "laneTimePlot", "rack", "portraitRing",
    "status",
)
FULL_RACK_REGIONS = ("singer", "expression", "style")
COLLAPSIBLE_REGIONS = ("workspaceTabs", "outputMeter")
ALWAYS_CONTROLS = (
    "classicToggle", "trackLabel", "gridLabel", "playButton", "positionReadout", "tempoReadout",
    "meterReadout",
)
WORKSPACES = ("sing", "voice", "tune", "mix", "export")
LANES = ("dynamics", "formant", "breath", "tension", "air", "gender", "growl")
ALWAYS_NODES = (
    "shell.classic", "shell.lane", "shell.mode.emo", "shell.mode.scene",
    "shell.settings", "shell.status", "shell.waveform",
) + tuple(f"shell.lane-tab.{lane}" for lane in LANES)
NOTE_LIMIT = 256
# Everything the shell may publish while the modal inspector is open (plus the shell.knob.* sliders).
MODAL_NODES = {"shell", "shell.inspector", "shell.change-voice", "shell.style", "voice.identity",
               "shell.status", "shell.render-progress"}
# The evidence (frame, semantics, geometry) is the last presented frame; the app logs its render
# state later, after shutdown. A render can finish in between, so the log may be the frame's state
# or a legal successor of it, never an earlier or unrelated one.
RENDER_SUCCESSORS = {
    "queued": {"queued", "rendering", "ready", "failed", "cancelled"},
    "rendering": {"rendering", "ready", "failed", "cancelled"},
}


def frame_render_state(semantic: dict[str, Any]) -> str | None:
    """The render state the presented frame's status node reported ("READY: ..." -> "ready")."""
    for node in semantic.get("nodes") or []:
        if node.get("id") == "shell.status":
            value = str(node.get("value", ""))
            return value.split(":", 1)[0].strip().lower() or None
    return None
# Rack controls are on screen with the full rack, or in the compact presentations only while the
# singer inspector is open (then they lie inside it).
RACK_CONTROLS_SHOWN = lambda geometry: geometry.get("rack") == "full" or geometry.get("inspectorOpen") is True  # noqa: E731


def check_geometry(geometry: dict[str, Any], contract: dict[str, Any], *,
                   expected: dict[str, Any]) -> dict[str, Any]:
    """expected: {"viewport": [w, h], "mode": "emo"|"scene", optional "inspectorOpen": bool,
    optional "workspace": "sing"|"tune"|"mix"}."""
    canonical = contract["canonical"]
    tolerance = float(canonical["regionTolerancePoints"])
    failures: list[str] = []
    regions = geometry.get("regions") or {}
    controls = geometry.get("controls") or {}
    size = geometry.get("logicalSize")
    if not (isinstance(size, list) and len(size) == 2 and positive([0, 0, *size])):
        return {"failures": ["logicalSize missing or invalid"], "result": "FAIL"}
    width, height = size
    # Identity: the snapshot must be the requested viewport, mode and workspace, presented.
    if [width, height] != list(expected["viewport"]):
        failures.append(f"logicalSize {size} is not the requested viewport {expected['viewport']}")
    if geometry.get("mode") != expected["mode"]:
        failures.append(f"mode {geometry.get('mode')} is not the requested {expected['mode']}")
    wanted_workspace = expected.get("workspace", "sing")
    if geometry.get("workspace") != wanted_workspace:
        failures.append(f"workspace {geometry.get('workspace')} is not {wanted_workspace}")
    if geometry.get("presented") is not True:
        failures.append("the shell did not present the frame")
    if geometry.get("deviceScale") not in canonical["deviceScales"]:
        failures.append(f"deviceScale {geometry.get('deviceScale')} is not one of {canonical['deviceScales']}")
    presentation = geometry.get("rack")
    wanted = spec_rack_presentation(float(width))
    if presentation != wanted:
        failures.append(f"rack presentation {presentation} where section 3.4 requires {wanted}")
    inspector_open = geometry.get("inspectorOpen") is True
    if inspector_open != bool(expected.get("inspectorOpen", False)):
        failures.append(f"inspector open={inspector_open}, requested {bool(expected.get('inspectorOpen', False))}")
    if presentation == "full" and inspector_open:
        failures.append("the full rack has no inspector")

    at_canonical = [width, height] == list(canonical["logicalSize"])
    required = list(ALWAYS_REGIONS)
    if width >= 720:
        required += ["wordmark", "modeSwitch"]
    if RACK_CONTROLS_SHOWN(geometry):
        required += FULL_RACK_REGIONS
    if inspector_open:
        required.append("inspector")
    if at_canonical:
        required += COLLAPSIBLE_REGIONS
    for name in required:
        if not positive(regions.get(name)):
            failures.append(f"region {name}: missing or empty")
    required_controls = list(ALWAYS_CONTROLS)
    if RACK_CONTROLS_SHOWN(geometry):
        required_controls += ["singerChange"] + [f"knob{i}" for i in range(len(KNOBS))]
    if presentation != "full":
        required_controls.append("inspectorButton")
    if positive(regions.get("workspaceTabs")):
        required_controls += [f"workspaceTab{i}" for i in range(len(WORKSPACES))]
    else:
        required_controls.append("workspaceMenuButton")
    for name in required_controls:
        if not positive(controls.get(name)):
            failures.append(f"control {name}: missing or empty")
    if inspector_open and positive(regions.get("inspector")):
        # Everything the inspector carries lies inside it.
        for name in ("singer", "expression", "style"):
            if positive(regions.get(name)) and not contains(regions["inspector"], regions[name]):
                failures.append(f"{name}: outside the inspector")
        for name in ["singerChange"] + [f"knob{i}" for i in range(len(KNOBS))]:
            if positive(controls.get(name)) and not contains(regions["inspector"], controls[name]):
                failures.append(f"{name}: outside the inspector")

    deviations: dict[str, list[float]] = {}
    if at_canonical:
        for region in canonical["regions"]:
            actual = regions.get(region["id"])
            if not positive(actual):
                continue  # already reported above
            delta = [round(a - e, 3) for a, e in zip(actual, region["rect"])]
            # The tolerance governs edges: left, top, right, bottom.
            edges = [delta[0], delta[1], delta[0] + delta[2], delta[1] + delta[3]]
            if any(abs(edge) > tolerance for edge in edges):
                deviations[region["id"]] = edges
                failures.append(f"{region['id']}: edges off by {edges} pt")
    visible = {name: rect for name, rect in regions.items() if positive(rect)}
    client = [0.0, 0.0, float(width), float(height)]
    parents = {region["id"]: region["parent"] for region in canonical["regions"]}
    for name, parent in parents.items():
        if inspector_open and parent == "rack":
            continue  # the inspector's cards lie in the inspector (checked above), not the rack column
        if name in visible and (parent == "client" or parent in visible):
            outer = client if parent == "client" else visible[parent]
            if not contains(outer, visible[name]):
                failures.append(f"{name}: outside {parent}")
    for group in canonical["nonOverlappingGroups"]:
        present = [name for name in group if name in visible]
        for index, first in enumerate(present):
            for second in present[index + 1:]:
                if inspector_open and {first, second} == {"singer", "style"}:
                    # In the inspector the style is a line of the singer row, not its own card.
                    if not contains(visible["singer"], visible["style"]):
                        failures.append("style: outside the inspector's singer row")
                    continue
                if overlaps(visible[first], visible[second]):
                    failures.append(f"{first} overlaps {second}")
    axis = [visible.get(name) for name in canonical["sharedTimeAxis"]]
    if any(rect is None for rect in axis) or len({(rect[0], rect[2]) for rect in axis}) != 1:
        failures.append("ruler, grid and lane plot do not share one time axis")
    rack = regions["rack"] if positive(regions.get("rack")) else [0, 0, 0, 0]
    rack_width = round(float(rack[2]), 3)
    spec_width = round(spec_rack_width(float(width)), 3)
    if abs(rack_width - spec_width) > 0.5:
        failures.append(f"rack width {rack_width:g} where section 3.4 requires {spec_width:g}")
    return {
        "logicalSize": [width, height],
        "comparedToCanonical": at_canonical,
        "tolerancePoints": tolerance,
        "edgeDeviations": deviations,
        "rackPresentation": presentation,
        "specRackPresentation": wanted,
        "rackWidth": rack_width,
        "specRackWidth": spec_width,
        "failures": failures,
        "result": "PASS" if not failures else "FAIL",
    }


def check_semantics(semantic: dict[str, Any], geometry: dict[str, Any], *,
                    expected_notes: int, render_state: str | None,
                    workspace: str = "sing", expected_tracks: int | None = None) -> dict[str, Any]:
    """expected_notes: notes in the fixture; render_state: the state the app logged at exit;
    workspace: a TUNE or MIX capture covers the score with that workspace's own nodes;
    expected_tracks: the fixture's track count, bounding the MIX strips."""
    failures: list[str] = []
    size = geometry.get("logicalSize") or [0, 0]
    client = [0.0, 0.0, float(size[0]), float(size[1])]
    regions = geometry.get("regions") or {}
    controls = geometry.get("controls") or {}
    nodes = semantic.get("nodes") or []
    by_id: dict[str, dict[str, Any]] = {}
    for node in nodes:
        if node["id"] in by_id:
            failures.append(f"duplicate id {node['id']}")
        by_id[node["id"]] = node
        bounds = node["bounds"]
        if node["parent"] and positive(bounds) and not contains(client, bounds):
            failures.append(f"{node['id']}: bounds outside the client")

    # Required nodes follow the captured frame; the state logged later is only checked below as a
    # legal progression from it.
    frame_state = frame_render_state(semantic)
    covered = geometry.get("inspectorOpen") is True
    body = workspace if workspace in WORKSPACE_STATES else None
    rack_nodes = ["shell.change-voice", "shell.style"] + [f"shell.knob.{knob}" for knob in KNOBS]
    if covered:
        # The open inspector is modal: it and the read-only status are all that is published.
        required = ["shell.inspector", "shell.status"] + rack_nodes
        for node_id in by_id:
            if node_id not in MODAL_NODES and not node_id.startswith("shell.knob."):
                failures.append(f"{node_id}: published under the open inspector")
    else:
        required = list(ALWAYS_NODES)
        if float(size[0]) < 720:
            required = [node for node in required if not node.startswith("shell.mode.")]
        if body:
            # The body replaces the grid and lane: none of their nodes, at least one of its own,
            # each inside the area it owns (the editor and lane cards).
            required = [node for node in required
                        if not node.startswith("shell.lane") and node != "shell.waveform"]
            prefix = f"shell.{body}."
            own = [node for node in by_id.values() if node["id"].startswith(prefix)]
            if not own:
                failures.append(f"no {prefix}* node is published")
            # The workspace's core controls, each with real bounds.
            if body == "tune":
                core = ["shell.tune.graph"] + [f"shell.tune.knob.{knob}" for knob in KNOBS]
            else:
                core = ["shell.mix.master", "shell.mix.audio-settings"]
                strips = [node["id"] for node in own
                          if re.fullmatch(r"shell\.mix\.track\.[0-9a-f]+", node["id"])]
                if not strips:
                    failures.append("no MIX channel strip is published")
                if expected_tracks is not None and len(strips) > expected_tracks:
                    failures.append(f"{len(strips)} strips for {expected_tracks} tracks")
                core += [f"{strip}.{control}" for strip in strips for control in MIX_STRIP_CONTROLS]
            for node_id in core:
                if node_id not in by_id:
                    failures.append(f"{node_id}: missing")
            for node in own:
                if not positive(node["bounds"]):
                    failures.append(f"{node['id']}: empty bounds")
            editor, lane = regions.get("editor"), regions.get("lane")
            if positive(editor) and positive(lane):
                area = [editor[0], editor[1], editor[2], lane[1] + lane[3] - editor[1]]
                for node in own:
                    if positive(node["bounds"]) and not contains(area, node["bounds"]):
                        failures.append(f"{node['id']}: outside the {body} workspace area")
            others = [f"shell.{name}." for name in WORKSPACE_STATES + ("export",) if name != body]
            for node_id in by_id:
                if node_id in SCORE_IDS or node_id.startswith(SCORE_PREFIXES) or \
                        node_id.startswith(tuple(others)):
                    failures.append(f"{node_id}: published under the {body} workspace")
        if RACK_CONTROLS_SHOWN(geometry):
            required += rack_nodes
        if geometry.get("rack") != "full":
            required.append("shell.inspector")
        if positive(regions.get("workspaceTabs")):
            required += [f"shell.workspace.{name}" for name in WORKSPACES]
        else:
            required.append("shell.workspace-menu")
    if frame_state in ("rendering", "queued"):
        required.append("shell.render-progress")
    for node_id in required:
        if node_id not in by_id:
            failures.append(f"{node_id}: not published")
    for node_id, region in SEMANTIC_REGIONS.items():
        node = by_id.get(node_id)
        if node is not None and node["bounds"] != regions.get(region):
            failures.append(f"{node_id}: bounds {node['bounds']} != layout {region}")
    for index, knob in enumerate(KNOBS):
        node = by_id.get(f"shell.knob.{knob}")
        if node is not None and node["bounds"] != controls.get(f"knob{index}"):
            failures.append(f"{node['id']}: bounds differ from knob{index}")
    for node_id, control in (("shell.inspector", "inspectorButton"), ("shell.change-voice", "singerChange")):
        node = by_id.get(node_id)
        if node is not None and node["bounds"] != controls.get(control):
            failures.append(f"{node_id}: bounds differ from {control}")
    menu = by_id.get("shell.workspace-menu")
    if menu is not None and menu["bounds"] != controls.get("workspaceMenuButton"):
        failures.append("shell.workspace-menu: bounds differ from workspaceMenuButton")
    if not RACK_CONTROLS_SHOWN(geometry):
        for node_id in by_id:
            if node_id.startswith("shell.knob.") or node_id in ("shell.change-voice", "shell.style"):
                failures.append(f"{node_id}: published while the inspector is closed")
    for index, name in enumerate(WORKSPACES):
        node = by_id.get(f"shell.workspace.{name}")
        if node is not None and node["bounds"] != controls.get(f"workspaceTab{index}"):
            failures.append(f"{node['id']}: bounds differ from workspaceTab{index}")

    # Notes: the tree must virtualize exactly the fixture's notes and list them up to the limit;
    # none while the inspector covers the score.
    expected_notes = 0 if covered or body else expected_notes
    count = semantic.get("virtualizedNoteCount")
    listed = semantic.get("notes")
    if count != expected_notes:
        failures.append(f"virtualizedNoteCount {count} is not the fixture's {expected_notes} notes")
    listed_count = len(listed) if isinstance(listed, list) else 0
    if not isinstance(listed, list) or listed_count != min(expected_notes, NOTE_LIMIT):
        failures.append(f"{listed_count} note nodes listed; expected {min(expected_notes, NOTE_LIMIT)}")
    if not covered and not body and expected_notes > 0 and frame_state == "ready":
        grid = regions.get("grid")
        if not positive(grid) or not any(positive(note.get("bounds")) and overlaps(note["bounds"], grid)
                   for note in listed or []):
            failures.append("ready phrase has no note visible inside the grid")
    # The frame's status and the state the app logged at exit must be the same state or a legal
    # progression (the frame is earlier).
    allowed = RENDER_SUCCESSORS.get(frame_state or "", {frame_state})
    if not render_state or not frame_state or render_state not in allowed:
        failures.append(f"frame status {frame_state} is not the logged state {render_state} "
                        "or an earlier stage of it")
    return {
        "nodes": len(nodes),
        "virtualizedNotes": count,
        "frameRenderState": frame_state,
        "focused": semantic.get("focused"),
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


def stop_child(process: Any, reason: str) -> tuple[str, str, str]:
    """Kill and reap only this capture's child, draining what it wrote."""
    process.kill()
    try:
        drained = process.communicate(timeout=10.0)
    except subprocess.TimeoutExpired:
        return "", "", f"{reason}; the killed app could not be reaped within 10 s"
    stdout, stderr = drained if isinstance(drained, tuple) and len(drained) == 2 else ("", "")
    return (stdout if isinstance(stdout, str) else ""), (stderr if isinstance(stderr, str) else ""), reason


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
    environment.pop("SEAM_UI_INSPECTOR", None)
    if state == INSPECTOR_STATE:
        environment["SEAM_UI_INSPECTOR"] = "open"
    if state in WORKSPACE_STATES:
        environment["SEAM_UI_WORKSPACE"] = state
    record: dict[str, Any] = {
        "id": name, "mode": mode, "state": state, "viewport": list(viewport),
        "command": [os.path.relpath(part, ROOT) if part.startswith(str(ROOT)) else part
                    for part in command[1:]],
    }
    started = time.monotonic()
    # One deadline covers startup, the window capture and shutdown.
    deadline = started + (close_ms + args.overrun_ms) / 1000.0
    process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    appkit_note = "not captured"
    failure: str | None = None
    while process.poll() is None and not window_id.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    if process.poll() is None and not window_id.exists():
        failure = "the app opened no window before the capture deadline"
    elif process.poll() is None and not args.no_appkit:
        lead_ms = min(args.appkit_lead_ms, close_ms * 0.25)
        # The window server needs a moment after the window opens before it can be captured.
        capture_at = max(started + (close_ms - lead_ms) / 1000.0, time.monotonic() + 0.25)
        time.sleep(max(0.0, min(capture_at, deadline) - time.monotonic()))
        wid = window_id.read_text().strip()
        try:
            shot = subprocess.run(["screencapture", "-x", "-o", "-l", wid, str(appkit_raw)],
                                  capture_output=True, text=True, check=False,
                                  timeout=max(0.1, deadline - time.monotonic()))
            appkit_note = ("captured" if shot.returncode == 0 and appkit_raw.is_file()
                           else f"screencapture failed: {shot.stderr.strip() or shot.returncode}")
        except subprocess.TimeoutExpired:
            appkit_note = "screencapture timed out at the capture deadline"
    if failure is None:
        try:
            stdout, stderr = process.communicate(timeout=max(0.1, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            stdout, stderr, failure = stop_child(process, "the app did not close before the capture deadline")
    else:
        stdout, stderr, failure = stop_child(process, failure)
    stdout = stdout if isinstance(stdout, str) else ""
    stderr = stderr if isinstance(stderr, str) else ""
    (folder / "app.log").write_text(stdout + ("\n# stderr\n" + stderr if stderr else ""))
    log = parse_log(stdout)
    record.update(exitCode=process.returncode, log=log, appkit=appkit_note)
    if failure is not None:
        record["error"] = f"{failure}; stderr: {stderr.strip()[-400:]}"
        return record
    if process.returncode != 0 or not ppm.is_file() or not (evidence / "geometry.json").is_file():
        record["error"] = f"exit {process.returncode}; stderr: {stderr.strip()[-400:]}"
        return record
    record["observedRenderState"] = log.get("render_state", "unknown")
    for part in ("geometry", "semantic-bounds", "performance"):
        record[part] = json.loads((evidence / f"{part}.json").read_text())
    # A capture shows its state when the presented frame shows it, whatever the render did after.
    record["frameRenderState"] = frame_render_state(record["semantic-bounds"])
    expected = {"ready": "ready", INSPECTOR_STATE: "ready", "failed": "failed", "dense-overlap": "ready",
                "rendering": "rendering", "empty": None, "tune": "ready", "mix": "ready"}[state]
    record["stateReached"] = expected is None or record["frameRenderState"] == expected
    return record

def process_images(record: dict[str, Any], folder: Path, out: Path, *, want_appkit: bool) -> None:
    from PIL import Image

    captures = out / "captures"
    failures: list[str] = []
    record["imageCheck"] = {"failures": failures, "result": "FAIL"}
    software, _ = srgb_image(Image.open(folder / "software.ppm"), None)
    save_srgb(software, captures / f"{record['id']}-software.png")
    record["softwarePng"] = f"captures/{record['id']}-software.png"
    record["softwarePixels"] = list(software.size)
    geometry = record["geometry"]
    scale = geometry.get("deviceScale")
    size = geometry.get("logicalSize") or [0, 0]
    expected = [round(size[0] * scale), round(size[1] * scale)] if isinstance(scale, (int, float)) else None
    if expected is None or list(software.size) != expected:
        failures.append(f"software frame {list(software.size)} is not logicalSize x deviceScale {expected}")
    raw = folder / "appkit-raw.png"
    if not raw.is_file():
        if want_appkit:
            failures.append(f"no window capture ({record.get('appkit')})")
        record["imageCheck"]["result"] = "PASS" if not failures else "FAIL"
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
        failures.append(f"window capture {width}x{height} does not align with the software frame")
        save_srgb(appkit_srgb, out / record["appkitPng"])
        return
    client = appkit_srgb.crop((0, title_bar, width, height))
    record["appkitTitleBarPixels"] = title_bar
    record["appkitAlignment"] = "client area = capture minus the top title bar"
    save_srgb(client, out / record["appkitPng"])
    regions = geometry["regions"]
    rois = dict(ROIS)
    if record["state"] in WORKSPACE_STATES:
        # The body covers the grid and lane: compare it as one region, not as notes and lane.
        rois.pop("notes")
        rois.pop("lane")
        editor, lane = regions.get("editor"), regions.get("lane")
        if positive(editor) and positive(lane):
            regions = dict(regions, body=[editor[0], editor[1], editor[2],
                                          lane[1] + lane[3] - editor[1]])
            rois["body"] = "body"
    record["roi"] = {
        roi: roi_metrics(software, client, regions[region], float(scale))
        for roi, region in rois.items() if positive(regions.get(region))
    }
    record["imageCheck"]["result"] = "PASS" if not failures else "FAIL"


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
        # The compact presentations also show their singer inspector open (a capture state, not a
        # contract render state): its knobs, voice and style must be reachable and inside it.
        for viewport in requirements["viewports"]:
            if spec_rack_presentation(float(viewport[0])) != "full":
                matrix += [(mode, INSPECTOR_STATE, (viewport[0], viewport[1])) for mode in MODES]
    # TUNE and MIX cover the score with their own body (capture states on the ready fixture), at
    # the canonical size and the smallest compact contract viewport.
    compact = min((tuple(v) for v in requirements["viewports"]), key=lambda v: v[0] * v[1])
    for workspace in WORKSPACE_STATES:
        if args.states and workspace not in args.states.split(","):
            continue
        sizes = [CANONICAL_VIEWPORT] if args.canonical_only else [CANONICAL_VIEWPORT, compact]
        matrix += [(mode, workspace, size) for size in sizes for mode in MODES]
    return matrix


PARITY_KEYS = ("logicalSize", "deviceScale", "workspace", "presented", "rack", "compactHeader",
               "workspaceLabelsVisible", "outputMeterVisible", "regions", "controls")


def mode_parity(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Same state and viewport must have the same geometry in both modes (section 11.3).

    Every key needs exactly one capture per mode with geometry; a missing partner is a failure."""
    groups: dict[tuple[str, tuple[int, ...]], dict[str, list[dict[str, Any]]]] = {}
    for record in records:
        key = (record["state"], tuple(record["viewport"]))
        groups.setdefault(key, {}).setdefault(record["mode"], []).append(record)
    results = []
    for (state, viewport), by_mode in groups.items():
        entry = {"state": state, "viewport": list(viewport)}
        problems = [f"{len(by_mode.get(mode, []))} {mode} captures" for mode in MODES
                    if len(by_mode.get(mode, [])) != 1 or "geometry" not in by_mode[mode][0]]
        if problems:
            results.append(entry | {"result": "FAIL", "reason": "; ".join(problems) + " with geometry"})
            continue
        first, second = (by_mode[mode][0]["geometry"] for mode in MODES)
        differing = [key for key in PARITY_KEYS if key not in first or first.get(key) != second.get(key)]
        results.append(entry | {"result": "PASS" if not differing else "FAIL",
                                "reason": f"differs in {', '.join(differing)}" if differing else ""})
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
        "| Capture | Frame state | Logged at exit | State reached | Geometry | Semantics | Images | AppKit | Paint p50/p95 ms | Footprint MB |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for record in records:
        if "error" in record:
            lines.append(f"| {record['id']} | - | - | - | ERROR | ERROR | ERROR | {record.get('appkit', '-')} | - | - |")
            continue
        perf = record["performance"]
        paint = perf["paintMillis"]
        memory = perf["memory"].get("physFootprintBytes") or 0
        lines.append(
            f"| {record['id']} | {record['frameRenderState']} | {record['observedRenderState']} | "
            f"{'yes' if record['stateReached'] else 'NO'} | {record['geometryCheck']['result']} | "
            f"{record['semanticCheck']['result']} | {record['imageCheck']['result']} | {record['appkit']} | "
            f"{paint['p50']:.1f}/{paint['p95']:.1f} | {memory / 1048576:.0f} |")
    errored = [r for r in records if "error" in r]
    if errored:
        lines += ["", "## Capture errors", ""] + [f"- {r['id']}: {r['error']}" for r in errored]
    for title, key in (("Geometry failures", "geometryCheck"), ("Semantic failures", "semanticCheck"),
                       ("Image failures", "imageCheck")):
        lines += ["", f"## {title}", ""]
        failing = [r for r in records if r.get(key, {}).get("failures")]
        lines += [f"- {r['id']}: {'; '.join(r[key]['failures'])}" for r in failing] or ["- none"]
    lines += ["", "## Rack width against the responsive rule (section 3.4)", "",
              "Section 3.4 asks for a 44-point inspector drawer button below 860 points, a "
              "56-point rail below 1100 and the full rack above. A mismatch is a geometry "
              "failure.", "",
              "| Capture | Presentation | Spec | Width | Spec width |", "|---|---|---|---|---|"]
    for record in records:
        check = record.get("geometryCheck")
        if check and "rackWidth" in check and record["state"] == "ready":
            lines.append(f"| {record['id']} | {check['rackPresentation']} | "
                         f"{check['specRackPresentation']} | {check['rackWidth']:g} | "
                         f"{check['specRackWidth']:g} |")
    lines += ["", "## Mode parity (same state and viewport, EMO vs SCENE geometry)", ""]
    lines += [f"- {p['state']} {p['viewport'][0]}x{p['viewport'][1]}: {p['result']}"
              + (f" ({p['reason']})" if p.get("reason") else "") for p in parity]
    lines += ["", "## Software vs AppKit per region (pixels over channel delta 8, and max delta)", "",
              "The initial targets of section 11.1 are uncalibrated. The AppKit frame is taken "
              "shortly before the app closes and the software frame at close, so moving content "
              "(meters, progress) can differ legitimately.", "",
              "| Capture | " + " | ".join(ROI_COLUMNS) + " |", "|---|" + "---|" * len(ROI_COLUMNS)]
    for record in records:
        if "roi" in record:
            cells = []
            for roi in ROI_COLUMNS:
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
    parser.add_argument("--overrun-ms", type=int, default=60000,
                        help="how long past its auto-close a capture may take before its app is "
                             "killed (one deadline for startup, window capture and shutdown)")
    parser.add_argument("--no-build", action="store_true",
                        help="capture the existing binary without building it first (the manifest "
                             "then records the source binding as unverified)")
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
    build = build_app(args.app, args.no_build)
    out = (args.output or ROOT / "build/evidence/ui-fidelity" / source["candidate"]).resolve()
    if out.exists() and any(out.iterdir()):
        raise PacketError(f"output must be empty: {out}")
    (out / "captures").mkdir(parents=True, exist_ok=True)
    contract = json.loads(CONTRACT.read_text())
    base = json.loads(args.fixture.read_text())

    work = Path(tempfile.mkdtemp(prefix="seam-ui-fidelity-"))
    fixtures: dict[str, Path] = {}
    fixture_notes: dict[str, int] = {}
    fixture_tracks: dict[str, int] = {}
    fixture_hashes: dict[str, str] = {"base": sha256_file(args.fixture)}
    records: list[dict[str, Any]] = []
    try:
        for mode, state, viewport in build_matrix(args):
            if state not in fixtures:
                path = work / f"fixture-{state}.seam"
                derived = derive_fixture(base, state)
                path.write_text(json.dumps(derived, indent=2))
                fixtures[state] = path
                fixture_notes[state] = len(region_of(derived)["notes"])
                fixture_tracks[state] = (len(derived.get("vocalTracks") or []) +
                                         len(derived.get("audioTracks") or []))
                fixture_hashes[state] = sha256_file(path)
            project = work / f"{mode}-{state}-{viewport[0]}x{viewport[1]}.seam"
            shutil.copyfile(fixtures[state], project)
            print(f"capture {mode} {state} {viewport[0]}x{viewport[1]}", flush=True)
            record = capture(args, work, mode, state, viewport, project)
            if "error" not in record:
                record["geometryCheck"] = check_geometry(
                    record["geometry"], contract,
                    expected={"viewport": list(viewport), "mode": mode,
                              "inspectorOpen": state == INSPECTOR_STATE,
                              "workspace": state if state in WORKSPACE_STATES else "sing"})
                record["semanticCheck"] = check_semantics(
                    record["semantic-bounds"], record["geometry"],
                    expected_notes=fixture_notes[state], render_state=record["observedRenderState"],
                    workspace=state, expected_tracks=fixture_tracks[state])
                process_images(record, work / record["id"], out, want_appkit=not args.no_appkit)
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
        "build": build,
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
              if "error" not in r and ("FAIL" in (r["geometryCheck"]["result"], r["semanticCheck"]["result"],
                                                  r["imageCheck"]["result"])
                                       or not r["stateReached"])]
    failed += [f"parity {p['state']} {p['viewport']}" for p in parity if p["result"] == "FAIL"]
    scales = {r["geometry"].get("deviceScale") for r in records if "geometry" in r}
    if len(scales) > 1:
        failed.append(f"captures disagree on the device scale: {sorted(map(str, scales))}")
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
