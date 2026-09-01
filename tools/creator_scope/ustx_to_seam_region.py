from __future__ import annotations

from .evidence import JsonObject, JsonValue
from .ustx_study_contracts import (
    BridgeError,
    ConversionContext,
    IdAllocator,
    array_value,
    integer_value,
    object_value,
    optional_boolean,
    optional_number,
    optional_string,
    required,
    string_value,
)
from .ustx_study_time import MeterPoint, TempoPoint, next_beat_tick, scale_tick
from .ustx_to_seam_pitch import ImportPitchRequest, import_pitch_points


def import_region(
    value: JsonValue,
    index: int,
    allocator: IdAllocator,
    tempos: list[TempoPoint],
    meters: list[MeterPoint],
    context: ConversionContext,
    language: str,
) -> tuple[int, JsonObject]:
    path = f"ustx.voice_parts[{index}]"
    part = object_value(value, path)
    track_index = integer_value(required(part, "track_no", path), f"{path}.track_no")
    position = integer_value(required(part, "position", path), f"{path}.position")
    duration = integer_value(required(part, "duration", path), f"{path}.duration")
    if position < 0 or duration <= 0:
        raise BridgeError(f"{path} has an invalid time range")
    notes = array_value(required(part, "notes", path), f"{path}.notes")
    last_note_end = 1
    for note_index, note_value in enumerate(notes):
        note_path = f"{path}.notes[{note_index}]"
        note = object_value(note_value, note_path)
        start = integer_value(
            required(note, "position", note_path), f"{note_path}.position"
        )
        note_duration = integer_value(
            required(note, "duration", note_path), f"{note_path}.duration"
        )
        last_note_end = max(last_note_end, start + note_duration)
    openutau_duration = next_beat_tick(position + last_note_end, meters) - position
    if duration < openutau_duration:
        context.loss(
            "USTX_PART_DURATION_EXTENDED_BY_OPENUTAU",
            f"{path}.duration",
            f"OpenUtau extends part duration from {duration} to {openutau_duration} ticks",
        )
        duration = openutau_duration
    lyrics: list[JsonValue] = []
    seam_notes: list[JsonValue] = []
    pitch_by_tick: dict[int, tuple[JsonObject, str]] = {}
    previous_end: int | None = None
    previous_adjusted_tone = 0.0
    for note_index, note_value in enumerate(notes):
        note_path = f"{path}.notes[{note_index}]"
        note = object_value(note_value, note_path)
        start = integer_value(
            required(note, "position", note_path), f"{note_path}.position"
        )
        note_duration = integer_value(
            required(note, "duration", note_path), f"{note_path}.duration"
        )
        tone = integer_value(required(note, "tone", note_path), f"{note_path}.tone")
        lyric = string_value(required(note, "lyric", note_path), f"{note_path}.lyric")
        if start < 0 or note_duration <= 0 or tone < 0 or tone > 127:
            raise BridgeError(f"{note_path} is outside SEAM note limits")
        tuning = optional_number(note, "tuning", 0.0, note_path)
        adjusted_tone = tone + tuning / 100.0
        pitch_value = note.get("pitch")
        snap_first = False
        if pitch_value is not None:
            pitch = object_value(pitch_value, f"{note_path}.pitch")
            snap_first = optional_boolean(
                pitch, "snap_first", True, f"{note_path}.pitch"
            )
        first_y_override = None
        if snap_first:
            first_y_override = (
                (previous_adjusted_tone - adjusted_tone) * 10.0
                if previous_end == start
                else 0.0
            )
        lyric_id = allocator.allocate()
        note_id = allocator.allocate()
        lyrics.append({"id": lyric_id, "surface": lyric, "language": language})
        seam_notes.append(
            {
                "id": note_id,
                "startTick": scale_tick(start, 480, 960, context, note_path),
                "durationTick": scale_tick(note_duration, 480, 960, context, note_path),
                "midiKey": tone,
                "lyricId": lyric_id,
                "articulation": "normal",
            }
        )
        imported_pitch = import_pitch_points(
            ImportPitchRequest(
                note=note,
                note_path=note_path,
                note_absolute_tick=position + start,
                note_duration=note_duration,
                part_tick=position,
                part_duration=duration,
                tempos=tempos,
                first_y_override=first_y_override,
                tuning_cents=tuning,
            ),
            context,
        )
        for point in imported_pitch:
            tick = integer_value(point["tick"], "pitch.tick")
            previous = pitch_by_tick.get(tick)
            if previous is not None and previous[0] != point:
                context.loss(
                    "USTX_PITCH_POINT_COLLISION",
                    note_path,
                    f"later note owns tick {tick}; replaced pitch from {previous[1]}",
                )
            pitch_by_tick[tick] = (point, note_path)
        vibrato = note.get("vibrato")
        if (
            isinstance(vibrato, dict)
            and optional_number(vibrato, "length", 0.0, f"{note_path}.vibrato") > 0
        ):
            context.loss(
                "USTX_NOTE_VIBRATO_NOT_REPRESENTABLE",
                f"{note_path}.vibrato",
                "schema 7 has no persisted note-vibrato field",
            )
        for field in ("phoneme_expressions", "phoneme_overrides"):
            values = note.get(field)
            if isinstance(values, list) and values:
                context.loss(
                    "USTX_PHONEME_DETAIL_NOT_REPRESENTABLE",
                    f"{note_path}.{field}",
                    "schema 7 cannot preserve this OpenUtau phoneme detail",
                )
        phonemizer = note.get("phonemizer")
        if isinstance(phonemizer, str) and phonemizer:
            context.loss(
                "USTX_NOTE_PHONEMIZER_REFERENCE_NOT_IMPORTED",
                f"{note_path}.phonemizer",
                "the inert note phonemizer reference was not resolved or persisted",
            )
        previous_end = start + note_duration
        previous_adjusted_tone = adjusted_tone
    curves = part.get("curves")
    if isinstance(curves, list) and curves:
        context.loss(
            "USTX_PART_CURVES_NOT_REPRESENTABLE",
            f"{path}.curves",
            "study bridge preserves note pitch points only",
        )
    pitch_values: list[JsonValue] = [
        pitch_by_tick[tick][0] for tick in sorted(pitch_by_tick)
    ]
    return track_index, {
        "id": allocator.allocate(),
        "name": optional_string(part, "name", f"Voice Part {index + 1}", path),
        "startTick": scale_tick(position, 480, 960, context, path),
        "durationTick": scale_tick(duration, 480, 960, context, path),
        "lyrics": lyrics,
        "notes": seam_notes,
        "phonemeOverrides": [],
        "unitSelectionOverrides": [],
        "seamOverrides": [],
        "pitchAutomation": pitch_values,
    }
