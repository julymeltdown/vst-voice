from __future__ import annotations

from fractions import Fraction

from .evidence import JsonObject
from .ustx_study_contracts import (
    BridgeError,
    ConversionContext,
    MAXIMUM_METER_EVENTS,
    MAXIMUM_TEMPO_EVENTS,
    array_value,
    integer_value,
    number_value,
    object_value,
    required,
)
from .ustx_study_time import MeterPoint, TempoPoint, rounded_fraction, scale_tick


def import_tempos(
    root: JsonObject, context: ConversionContext
) -> tuple[list[JsonObject], list[TempoPoint]]:
    values = array_value(required(root, "tempos", "ustx"), "ustx.tempos")
    if len(values) > MAXIMUM_TEMPO_EVENTS:
        raise BridgeError("USTX tempo count exceeds the study limit")
    points: list[TempoPoint] = []
    result: list[JsonObject] = []
    for index, value in enumerate(values):
        path = f"ustx.tempos[{index}]"
        item = object_value(value, path)
        position = integer_value(required(item, "position", path), f"{path}.position")
        bpm = number_value(required(item, "bpm", path), f"{path}.bpm")
        if position < 0 or bpm <= 0 or bpm > 1000:
            raise BridgeError(f"{path} is outside SEAM tempo limits")
        points.append(TempoPoint(position, bpm))
    points.sort(key=lambda point: point.tick)
    if not points or points[0].tick != 0:
        raise BridgeError("ustx.tempos must begin at position 0")
    if len({point.tick for point in points}) != len(points):
        raise BridgeError("ustx.tempos contains duplicate positions")
    for index, point in enumerate(points):
        result.append(
            {
                "tick": scale_tick(
                    point.tick, 480, 960, context, f"ustx.tempos[{index}].position"
                ),
                "bpm": point.bpm,
            }
        )
    return result, points


def import_meters(
    root: JsonObject, context: ConversionContext
) -> tuple[list[JsonObject], list[MeterPoint]]:
    values = array_value(
        required(root, "time_signatures", "ustx"), "ustx.time_signatures"
    )
    if len(values) > MAXIMUM_METER_EVENTS:
        raise BridgeError("USTX meter count exceeds the study limit")
    result: list[JsonObject] = []
    points: list[MeterPoint] = []
    previous_bar = 0
    previous_tick = Fraction(0)
    previous_numerator = 4
    previous_denominator = 4
    for index, value in enumerate(values):
        path = f"ustx.time_signatures[{index}]"
        item = object_value(value, path)
        bar = integer_value(
            required(item, "bar_position", path), f"{path}.bar_position"
        )
        numerator = integer_value(
            required(item, "beat_per_bar", path), f"{path}.beat_per_bar"
        )
        denominator = integer_value(
            required(item, "beat_unit", path), f"{path}.beat_unit"
        )
        if bar < previous_bar or numerator < 1 or numerator > 32:
            raise BridgeError(f"{path} is outside SEAM meter limits")
        if denominator not in {1, 2, 4, 8, 16, 32}:
            raise BridgeError(f"{path}.beat_unit is unsupported")
        previous_tick += Fraction(
            (bar - previous_bar) * 480 * 4 * previous_numerator,
            previous_denominator,
        )
        ustx_tick = rounded_fraction(
            previous_tick, context, f"{path}.bar_position", "METER_TICK_ROUNDED"
        )
        result.append(
            {
                "tick": scale_tick(ustx_tick, 480, 960, context, path),
                "numerator": numerator,
                "denominator": denominator,
            }
        )
        points.append(MeterPoint(ustx_tick, numerator, denominator))
        previous_bar = bar
        previous_numerator = numerator
        previous_denominator = denominator
    if not result or result[0]["tick"] != 0:
        raise BridgeError("ustx.time_signatures must begin at bar 0")
    return result, points
