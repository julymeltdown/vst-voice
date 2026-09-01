from __future__ import annotations

from pathlib import Path

from .evidence import JsonObject, JsonValue
from .seam_to_ustx import convert_seam_to_ustx
from .ustx_study_contracts import (
    BridgeError,
    ConversionContext,
    STUDY_BRIDGE_VERSION,
)
from .ustx_study_io import (
    json_text,
    sha256,
    TextOutput,
    write_pair_new,
    yaml_text,
)
from .ustx_study_load import load_json, load_ustx
from .ustx_to_seam import ImportOptions, convert_ustx_to_seam


LIMITATIONS: tuple[str, ...] = (
    "Study-only prototype; not a production interchange implementation.",
    "Only USTX 0.9 and SEAM schema 7 are accepted.",
    "Singer, renderer, phonemizer, resampler, wavtool, and package references are never resolved.",
    "Note vibrato, USTX curves, wave parts, style, and unsupported phoneme data are reported as losses.",
    "The 4 MiB parser limit and trusted-fixture workflow are narrower than the planned production contract.",
)


def _count(project: JsonObject, direction: str) -> JsonObject:
    track_key = "vocalTracks" if direction == "USTX_TO_SEAM" else "tracks"
    part_key = "vocalTracks" if direction == "USTX_TO_SEAM" else "voice_parts"
    tracks = project.get(track_key)
    track_count = len(tracks) if isinstance(tracks, list) else 0
    if direction == "USTX_TO_SEAM":
        part_count = 0
        note_count = 0
        if isinstance(tracks, list):
            for track_value in tracks:
                if not isinstance(track_value, dict):
                    continue
                regions = track_value.get("regions")
                if not isinstance(regions, list):
                    continue
                part_count += len(regions)
                for region_value in regions:
                    if isinstance(region_value, dict):
                        notes = region_value.get("notes")
                        if isinstance(notes, list):
                            note_count += len(notes)
    else:
        parts = project.get(part_key)
        part_count = len(parts) if isinstance(parts, list) else 0
        note_count = 0
        if isinstance(parts, list):
            for part_value in parts:
                if isinstance(part_value, dict):
                    notes = part_value.get("notes")
                    if isinstance(notes, list):
                        note_count += len(notes)
    return {"tracks": track_count, "parts": part_count, "notes": note_count}


def _report(
    direction: str,
    source: Path,
    source_bytes: bytes,
    target: Path,
    target_bytes: bytes,
    converted: JsonObject,
    context: ConversionContext,
) -> JsonObject:
    losses: list[JsonValue] = list(context.losses)
    return {
        "recordType": "creator-ustx-study-conversion",
        "recordVersion": 1,
        "toolVersion": STUDY_BRIDGE_VERSION,
        "studyOnly": True,
        "direction": direction,
        "source": {
            "filename": source.name,
            "bytes": len(source_bytes),
            "sha256": sha256(source_bytes),
        },
        "target": {
            "filename": target.name,
            "bytes": len(target_bytes),
            "sha256": sha256(target_bytes),
        },
        "statistics": _count(converted, direction),
        "losses": losses,
        "lossCount": len(losses),
        "unsafeReferencesResolved": False,
        "limitations": list(LIMITATIONS),
    }


def _preflight(target: Path, report: Path) -> None:
    if target == report:
        raise BridgeError("conversion output and report paths must be different")
    for path in (target, report):
        if path.exists() or path.is_symlink():
            raise BridgeError(f"Refusing to overwrite existing output: {path}")


def import_ustx(
    source: Path,
    target: Path,
    report_path: Path,
    options: ImportOptions,
) -> JsonObject:
    _preflight(target, report_path)
    payload, source_bytes = load_ustx(source)
    context = ConversionContext("USTX_TO_SEAM")
    project = convert_ustx_to_seam(payload, options, context)
    target_text = json_text(project)
    target_bytes = target_text.encode("utf-8")
    report = _report(
        context.direction,
        source,
        source_bytes,
        target,
        target_bytes,
        project,
        context,
    )
    write_pair_new(
        TextOutput(target, target_text),
        TextOutput(report_path, json_text(report)),
    )
    return report


def export_ustx(source: Path, target: Path, report_path: Path) -> JsonObject:
    _preflight(target, report_path)
    payload, source_bytes = load_json(source)
    context = ConversionContext("SEAM_TO_USTX")
    project = convert_seam_to_ustx(payload, context)
    target_text = yaml_text(project)
    target_bytes = target_text.encode("utf-8")
    report = _report(
        context.direction,
        source,
        source_bytes,
        target,
        target_bytes,
        project,
        context,
    )
    write_pair_new(
        TextOutput(target, target_text),
        TextOutput(report_path, json_text(report)),
    )
    return report
