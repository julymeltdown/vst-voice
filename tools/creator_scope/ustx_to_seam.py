from __future__ import annotations

from dataclasses import dataclass
import math

from .evidence import JsonObject, JsonValue
from .ustx_study_contracts import (
    BridgeError,
    ConversionContext,
    IdAllocator,
    MAXIMUM_NOTES,
    MAXIMUM_PARTS,
    MAXIMUM_TRACKS,
    array_value,
    object_value,
    optional_boolean,
    optional_number,
    optional_string,
    required,
)
from .ustx_to_seam_maps import import_meters, import_tempos
from .ustx_to_seam_region import import_region


@dataclass(frozen=True, slots=True)
class ImportOptions:
    voicebank_id: str = "study.unresolved.voicebank"
    voicebank_version: str = "0.0.0-study"
    voicebank_content_hash: str = ""
    character_id: str = "study.unresolved.character"
    character_version: str = "0.0.0-study"
    language: str = "ja"


def _pan_matrix(pan: float) -> tuple[float, list[JsonValue]]:
    clamped = max(-1.0, min(1.0, pan))
    angle = (clamped + 1.0) * math.pi / 4.0
    return clamped, [math.cos(angle), math.sin(angle)]


def convert_ustx_to_seam(
    root: JsonObject, options: ImportOptions, context: ConversionContext
) -> JsonObject:
    version = required(root, "ustx_version", "ustx")
    if str(version) != "0.9":
        raise BridgeError("study bridge supports only USTX version 0.9")
    tempo_map, tempos = import_tempos(root, context)
    meter_map, meters = import_meters(root, context)
    tracks = array_value(required(root, "tracks", "ustx"), "ustx.tracks")
    parts = array_value(root.get("voice_parts", []), "ustx.voice_parts")
    wave_parts = root.get("wave_parts")
    if isinstance(wave_parts, list) and wave_parts:
        context.loss(
            "USTX_WAVE_PARTS_NOT_IMPORTED",
            "ustx.wave_parts",
            "wave parts remain inert and are not imported by the vocal study bridge",
        )
    expressions = root.get("expressions")
    if isinstance(expressions, dict) and expressions:
        context.loss(
            "USTX_EXPRESSIONS_NOT_IMPORTED",
            "ustx.expressions",
            "renderer expression descriptors are outside the schema-7 study subset",
        )
    if len(tracks) > MAXIMUM_TRACKS or len(parts) > MAXIMUM_PARTS:
        raise BridgeError("USTX track or part count exceeds the study limit")
    note_count = sum(
        len(array_value(object_value(part, "voice_part").get("notes", []), "notes"))
        for part in parts
    )
    if note_count > MAXIMUM_NOTES:
        raise BridgeError("USTX note count exceeds the study limit")
    allocator = IdAllocator()
    regions_by_track: dict[int, list[JsonValue]] = {
        index: [] for index in range(len(tracks))
    }
    for index, value in enumerate(parts):
        track_index, region = import_region(
            value, index, allocator, tempos, meters, context, options.language
        )
        if track_index not in regions_by_track:
            raise BridgeError(f"ustx.voice_parts[{index}].track_no is out of range")
        regions_by_track[track_index].append(region)
    vocal_tracks: list[JsonValue] = []
    for index, value in enumerate(tracks):
        path = f"ustx.tracks[{index}]"
        track = object_value(value, path)
        raw_pan = optional_number(track, "pan", 0.0, path)
        pan, gains = _pan_matrix(raw_pan)
        if pan != raw_pan:
            context.loss("USTX_TRACK_PAN_CLAMPED", f"{path}.pan", "pan was clamped")
        singer = track.get("singer")
        if isinstance(singer, str) and singer:
            context.loss(
                "USTX_SINGER_REFERENCE_NOT_IMPORTED",
                f"{path}.singer",
                "the inert singer reference was not resolved or persisted",
            )
        phonemizer = track.get("phonemizer")
        if isinstance(phonemizer, str) and phonemizer:
            context.loss(
                "USTX_PHONEMIZER_REFERENCE_NOT_IMPORTED",
                f"{path}.phonemizer",
                "the inert phonemizer reference was not resolved or persisted",
            )
        renderer_settings = track.get("renderer_settings")
        if isinstance(renderer_settings, dict):
            for field, code in (
                ("renderer", "USTX_RENDERER_REFERENCE_NOT_IMPORTED"),
                ("resampler", "USTX_RESAMPLER_REFERENCE_NOT_IMPORTED"),
                ("wavtool", "USTX_WAVTOOL_REFERENCE_NOT_IMPORTED"),
            ):
                reference = renderer_settings.get(field)
                if isinstance(reference, str) and reference:
                    context.loss(
                        code,
                        f"{path}.renderer_settings.{field}",
                        f"the inert {field} reference was not resolved or persisted",
                    )
        mix_fx = track.get("mix_fx")
        if isinstance(mix_fx, dict) and mix_fx:
            context.loss(
                "USTX_TRACK_MIX_FX_NOT_IMPORTED",
                f"{path}.mix_fx",
                "track effects are outside the schema-7 study subset",
            )
        voice_colors = track.get("voice_color_names")
        if isinstance(voice_colors, list) and any(
            isinstance(value, str) and value for value in voice_colors
        ):
            context.loss(
                "USTX_VOICE_COLORS_NOT_IMPORTED",
                f"{path}.voice_color_names",
                "voice colors require the post-study style contract",
            )
        track_expressions = track.get("track_expressions")
        if isinstance(track_expressions, list) and track_expressions:
            context.loss(
                "USTX_TRACK_EXPRESSIONS_NOT_IMPORTED",
                f"{path}.track_expressions",
                "track expressions are outside the schema-7 study subset",
            )
        vocal_tracks.append(
            {
                "id": allocator.allocate(),
                "name": optional_string(
                    track, "track_name", f"Track {index + 1}", path
                ),
                "voicebank": {
                    "id": options.voicebank_id,
                    "version": options.voicebank_version,
                    "contentHash": options.voicebank_content_hash,
                },
                "character": {
                    "id": options.character_id,
                    "version": options.character_version,
                },
                "gainDb": optional_number(track, "volume", 0.0, path),
                "pan": pan,
                "muted": optional_boolean(track, "mute", False, path),
                "solo": optional_boolean(track, "solo", False, path),
                "outputRoute": {
                    "busId": "1",
                    "matrix": {
                        "sourceChannels": 1,
                        "destinationChannels": 2,
                        "gains": gains,
                    },
                },
                "regions": regions_by_track[index],
            }
        )
    tempo_values: list[JsonValue] = list(tempo_map)
    meter_values: list[JsonValue] = list(meter_map)
    return {
        "formatId": "com.project-seam.project",
        "schemaVersion": 7,
        "projectId": "1",
        "name": optional_string(root, "name", "Imported USTX Study", "ustx")
        or "Imported USTX Study",
        "ppq": 960,
        "tempoMap": tempo_values,
        "meterMap": meter_values,
        "settings": {
            "sampleRate": 48000.0,
            "characterDisplay": "minimal",
            "snapEnabled": True,
            "snapGrid": 240,
            "hostStartOffsetTick": 0,
            "technicalLanes": [
                {"mode": "auto", "expandedHeight": 120.0} for _ in range(4)
            ],
        },
        "routing": {
            "deviceOutputChannels": 2,
            "masterBus": "1",
            "buses": [
                {
                    "id": "1",
                    "name": "Master",
                    "channelCount": 2,
                    "gainDb": 0.0,
                    "muted": False,
                    "solo": False,
                }
            ],
            "sends": [],
            "deviceRoutes": [
                {
                    "sourceBus": "1",
                    "matrix": {
                        "sourceChannels": 2,
                        "destinationChannels": 2,
                        "gains": [1.0, 0.0, 0.0, 1.0],
                    },
                    "gainDb": 0.0,
                    "enabled": True,
                }
            ],
        },
        "vocalTracks": vocal_tracks,
        "audioTracks": [],
    }
