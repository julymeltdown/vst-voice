from __future__ import annotations

from dataclasses import dataclass

from .evidence import JsonObject
from .ustx_study_contracts import (
    ConversionContext,
    array_value,
    number_value,
    object_value,
    optional_string,
    required,
)
from .ustx_study_time import TempoPoint, pitch_milliseconds_to_tick, scale_tick


@dataclass(frozen=True, slots=True)
class ImportPitchRequest:
    note: JsonObject
    note_path: str
    note_absolute_tick: int
    note_duration: int
    part_tick: int
    part_duration: int
    tempos: list[TempoPoint]
    first_y_override: float | None
    tuning_cents: float


def import_pitch_points(
    request: ImportPitchRequest,
    context: ConversionContext,
) -> list[JsonObject]:
    pitch_value = request.note.get("pitch")
    values = []
    if pitch_value is not None:
        pitch = object_value(pitch_value, f"{request.note_path}.pitch")
        values = array_value(pitch.get("data", []), f"{request.note_path}.pitch.data")
    result_by_tick: dict[int, JsonObject] = {}
    for index, value in enumerate(values):
        path = f"{request.note_path}.pitch.data[{index}]"
        point = object_value(value, path)
        offset_ms = number_value(required(point, "x", path), f"{path}.x")
        y = number_value(required(point, "y", path), f"{path}.y")
        if index == 0 and request.first_y_override is not None:
            if y != request.first_y_override:
                context.loss(
                    "USTX_SNAP_FIRST_MATERIALIZED",
                    path,
                    f"OpenUtau snap_first changes pitch Y from {y:g} to {request.first_y_override:g}",
                )
            y = request.first_y_override
        shape = optional_string(point, "shape", "io", path)
        absolute = pitch_milliseconds_to_tick(
            request.note_absolute_tick,
            offset_ms,
            request.tempos,
            480,
            context,
            f"{path}.x",
        )
        relative = absolute - request.part_tick
        if relative < 0 or relative > request.part_duration:
            context.loss(
                "USTX_PITCH_POINT_OUTSIDE_PART",
                path,
                "pitch point was omitted because it falls outside its voice part",
            )
            continue
        seam_tick = scale_tick(relative, 480, 960, context, f"{path}.x")
        if shape not in {"l", "io", "i", "o", "sp"}:
            context.loss(
                "USTX_PITCH_SHAPE_UNKNOWN", path, f"unknown shape {shape!r} used linear"
            )
            interpolation = "linear"
        elif shape == "l":
            interpolation = "linear"
        else:
            interpolation = "smooth"
            context.loss(
                "USTX_PITCH_SHAPE_APPROXIMATED",
                path,
                f"OpenUtau shape {shape} was approximated as SEAM smooth",
            )
        if seam_tick in result_by_tick:
            context.loss(
                "USTX_PITCH_POINT_COLLISION",
                path,
                "a rounded pitch point replaced an earlier point at the same tick",
            )
        result_by_tick[seam_tick] = {
            "tick": seam_tick,
            "cents": request.tuning_cents + y * 10.0,
            "interpolation": interpolation,
        }
    note_start = scale_tick(
        request.note_absolute_tick - request.part_tick,
        480,
        960,
        context,
        f"{request.note_path}.position",
    )
    note_end = scale_tick(
        request.note_absolute_tick + request.note_duration - request.part_tick,
        480,
        960,
        context,
        f"{request.note_path}.duration",
    )
    ordered_ticks = sorted(result_by_tick)
    if not ordered_ticks:
        result_by_tick[note_start] = {
            "tick": note_start,
            "cents": request.tuning_cents,
            "interpolation": "linear",
        }
        result_by_tick[note_end] = {
            "tick": note_end,
            "cents": request.tuning_cents,
            "interpolation": "linear",
        }
    else:
        first = result_by_tick[ordered_ticks[0]]
        last = result_by_tick[ordered_ticks[-1]]
        if ordered_ticks[0] > note_start:
            result_by_tick[note_start] = {
                "tick": note_start,
                "cents": first["cents"],
                "interpolation": first["interpolation"],
            }
        if ordered_ticks[-1] < note_end:
            result_by_tick[note_end] = {
                "tick": note_end,
                "cents": last["cents"],
                "interpolation": "linear",
            }
    return [result_by_tick[tick] for tick in sorted(result_by_tick)]
