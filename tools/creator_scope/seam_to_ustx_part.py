from __future__ import annotations

from .evidence import JsonObject, JsonValue
from .seam_to_ustx_pitch import (
    ExportPitchRequest,
    export_note_pitch,
    index_pitch_points,
)
from .ustx_study_contracts import (
    BridgeError,
    ConversionContext,
    array_value,
    integer_value,
    object_value,
    optional_string,
    required,
    string_value,
)
from .ustx_study_time import MeterPoint, TempoPoint, next_beat_tick, scale_tick


def export_part(
    value: JsonValue,
    track_index: int,
    region_index: int,
    ppq: int,
    tempos: list[TempoPoint],
    meters: list[MeterPoint],
    context: ConversionContext,
) -> JsonObject:
    path = f"project.vocalTracks[{track_index}].regions[{region_index}]"
    region = object_value(value, path)
    start = integer_value(required(region, "startTick", path), f"{path}.startTick")
    duration = integer_value(
        required(region, "durationTick", path), f"{path}.durationTick"
    )
    if start < 0 or duration <= 0:
        raise BridgeError(f"{path} has an invalid time range")
    lyric_values = array_value(required(region, "lyrics", path), f"{path}.lyrics")
    lyrics: dict[str, str] = {}
    for index, value in enumerate(lyric_values):
        lyric_path = f"{path}.lyrics[{index}]"
        lyric = object_value(value, lyric_path)
        lyric_id = string_value(required(lyric, "id", lyric_path), f"{lyric_path}.id")
        lyrics[lyric_id] = string_value(
            required(lyric, "surface", lyric_path), f"{lyric_path}.surface"
        )
    pitch = array_value(
        required(region, "pitchAutomation", path), f"{path}.pitchAutomation"
    )
    pitch_ticks, pitch_entries = index_pitch_points(pitch, path)
    note_values = array_value(required(region, "notes", path), f"{path}.notes")
    notes: list[JsonValue] = []
    note_ranges: list[tuple[int, int]] = []
    last_note_end = 1
    for index, value in enumerate(note_values):
        note_path = f"{path}.notes[{index}]"
        note = object_value(value, note_path)
        position = integer_value(
            required(note, "startTick", note_path), f"{note_path}.startTick"
        )
        note_duration = integer_value(
            required(note, "durationTick", note_path), f"{note_path}.durationTick"
        )
        tone = integer_value(
            required(note, "midiKey", note_path), f"{note_path}.midiKey"
        )
        lyric_id = string_value(
            required(note, "lyricId", note_path), f"{note_path}.lyricId"
        )
        if lyric_id not in lyrics:
            raise BridgeError(f"{note_path}.lyricId references a missing lyric")
        articulation = optional_string(note, "articulation", "normal", note_path)
        if articulation != "normal":
            context.loss(
                "SEAM_NOTE_ARTICULATION_NOT_EXPORTED",
                f"{note_path}.articulation",
                f"SEAM articulation {articulation!r} has no study USTX mapping",
            )
        exported_position = scale_tick(
            position, ppq, 480, context, f"{note_path}.startTick"
        )
        exported_duration = scale_tick(
            note_duration, ppq, 480, context, f"{note_path}.durationTick"
        )
        last_note_end = max(last_note_end, exported_position + exported_duration)
        note_ranges.append((position, position + note_duration))
        notes.append(
            {
                "position": exported_position,
                "duration": exported_duration,
                "tone": tone,
                "lyric": lyrics[lyric_id],
                "pitch": {
                    "data": export_note_pitch(
                        ExportPitchRequest(
                            ticks=pitch_ticks,
                            entries=pitch_entries,
                            note_start=position,
                            note_end=position + note_duration,
                            region_start=start,
                            ppq=ppq,
                            tempos=tempos,
                            path=note_path,
                        ),
                        context,
                    ),
                    "snap_first": False,
                },
                "vibrato": {
                    "length": 0,
                    "period": 175,
                    "depth": 25,
                    "in": 10,
                    "out": 10,
                    "shift": 0,
                    "drift": 0,
                    "vol_link": 0,
                },
                "tuning": 0,
                "phoneme_expressions": [],
                "phoneme_overrides": [],
            }
        )
    range_index = 0
    ordered_ranges = sorted(note_ranges)
    for entry in pitch_entries:
        while (
            range_index < len(ordered_ranges)
            and ordered_ranges[range_index][1] < entry.tick
        ):
            range_index += 1
        if range_index >= len(ordered_ranges) or not (
            ordered_ranges[range_index][0]
            <= entry.tick
            <= ordered_ranges[range_index][1]
        ):
            context.loss(
                "SEAM_PITCH_POINT_OUTSIDE_NOTES",
                f"{path}.pitchAutomation[{entry.index}]",
                "region pitch point was not exported because no note owns its tick",
            )
    for field, code in (
        ("phonemeOverrides", "SEAM_PHONEME_OVERRIDES_NOT_EXPORTED"),
        ("unitSelectionOverrides", "SEAM_UNIT_OVERRIDES_NOT_EXPORTED"),
        ("seamOverrides", "SEAM_SEAM_OVERRIDES_NOT_EXPORTED"),
    ):
        values = region.get(field)
        if isinstance(values, list) and values:
            context.loss(code, f"{path}.{field}", "field is outside the study subset")
    part_position = scale_tick(start, ppq, 480, context, f"{path}.startTick")
    part_duration = scale_tick(duration, ppq, 480, context, f"{path}.durationTick")
    minimum_duration = (
        next_beat_tick(part_position + last_note_end, meters) - part_position
    )
    if part_duration < minimum_duration:
        context.loss(
            "SEAM_PART_DURATION_EXTENDED_FOR_OPENUTAU",
            f"{path}.durationTick",
            f"part duration extended from {part_duration} to {minimum_duration} USTX ticks",
        )
        part_duration = minimum_duration
    return {
        "name": optional_string(region, "name", f"Voice Part {region_index + 1}", path),
        "comment": "",
        "track_no": track_index,
        "position": part_position,
        "duration": part_duration,
        "curves": [],
        "notes": notes,
    }
