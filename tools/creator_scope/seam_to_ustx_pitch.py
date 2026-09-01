from __future__ import annotations

from bisect import bisect_left, bisect_right
from dataclasses import dataclass

from .evidence import JsonValue
from .ustx_study_contracts import (
    BridgeError,
    ConversionContext,
    integer_value,
    number_value,
    object_value,
    required,
    string_value,
)
from .ustx_study_time import TempoPoint, pitch_tick_to_milliseconds, scale_tick


@dataclass(frozen=True, slots=True)
class PitchEntry:
    tick: int
    index: int
    cents: float
    interpolation: str


@dataclass(frozen=True, slots=True)
class ExportPitchRequest:
    ticks: list[int]
    entries: list[PitchEntry]
    note_start: int
    note_end: int
    region_start: int
    ppq: int
    tempos: list[TempoPoint]
    path: str


def index_pitch_points(
    points: list[JsonValue], path: str
) -> tuple[list[int], list[PitchEntry]]:
    entries: list[PitchEntry] = []
    for index, value in enumerate(points):
        point_path = f"{path}.pitchAutomation[{index}]"
        point = object_value(value, point_path)
        tick = integer_value(required(point, "tick", point_path), f"{point_path}.tick")
        cents = number_value(
            required(point, "cents", point_path), f"{point_path}.cents"
        )
        interpolation = string_value(
            required(point, "interpolation", point_path),
            f"{point_path}.interpolation",
        )
        if tick < 0 or cents < -4800.0 or cents > 4800.0:
            raise BridgeError(f"{point_path} is outside SEAM pitch limits")
        entries.append(PitchEntry(tick, index, cents, interpolation))
    entries.sort(key=lambda entry: (entry.tick, entry.index))
    ticks = [entry.tick for entry in entries]
    if len(set(ticks)) != len(ticks):
        raise BridgeError(f"{path}.pitchAutomation ticks must be unique")
    return ticks, entries


def _value_at(ticks: list[int], entries: list[PitchEntry], tick: int) -> float:
    if tick <= ticks[0]:
        return entries[0].cents
    if tick >= ticks[-1]:
        return entries[-1].cents
    right_index = bisect_right(ticks, tick)
    left = entries[right_index - 1]
    right = entries[right_index]
    if left.interpolation == "step":
        return left.cents
    position = (tick - left.tick) / (right.tick - left.tick)
    if left.interpolation == "smooth":
        position = position * position * (3.0 - 2.0 * position)
    return left.cents * (1.0 - position) + right.cents * position


def _interpolation_at(ticks: list[int], entries: list[PitchEntry], tick: int) -> str:
    index = bisect_right(ticks, tick) - 1
    return entries[max(0, index)].interpolation


def _shape(interpolation: str, path: str, context: ConversionContext) -> str:
    if interpolation == "linear":
        return "l"
    if interpolation == "smooth":
        context.loss(
            "SEAM_PITCH_SHAPE_APPROXIMATED",
            path,
            "SEAM smooth interpolation was approximated as OpenUtau io",
        )
        return "io"
    context.loss(
        "SEAM_PITCH_SHAPE_APPROXIMATED",
        path,
        f"SEAM interpolation {interpolation!r} was approximated as linear",
    )
    return "l"


def export_note_pitch(
    request: ExportPitchRequest,
    context: ConversionContext,
) -> list[JsonValue]:
    if not request.entries:
        return [{"x": 0.0, "y": 0.0, "shape": "l"}]
    left = bisect_right(request.ticks, request.note_start)
    right = bisect_left(request.ticks, request.note_end)
    points = [
        PitchEntry(
            request.note_start,
            -1,
            _value_at(request.ticks, request.entries, request.note_start),
            _interpolation_at(request.ticks, request.entries, request.note_start),
        ),
        *request.entries[left:right],
        PitchEntry(
            request.note_end,
            -1,
            _value_at(request.ticks, request.entries, request.note_end),
            "linear",
        ),
    ]
    note_start_ustx = scale_tick(
        request.region_start + request.note_start,
        request.ppq,
        480,
        context,
        f"{request.path}.position",
    )
    result: list[JsonValue] = []
    for point in points:
        point_path = (
            f"{request.path}.pitchAutomation[{point.index}]"
            if point.index >= 0
            else f"{request.path}.pitchBoundary[{point.tick}]"
        )
        absolute = scale_tick(
            request.region_start + point.tick,
            request.ppq,
            480,
            context,
            f"{point_path}.tick",
        )
        result.append(
            {
                "x": pitch_tick_to_milliseconds(
                    note_start_ustx, absolute, request.tempos, 480
                ),
                "y": point.cents / 10.0,
                "shape": _shape(point.interpolation, point_path, context),
            }
        )
    return result
