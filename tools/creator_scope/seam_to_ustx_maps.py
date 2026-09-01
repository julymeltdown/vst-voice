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


def export_tempos(
    root: JsonObject, ppq: int, context: ConversionContext
) -> tuple[list[JsonObject], list[TempoPoint]]:
    values = array_value(required(root, "tempoMap", "project"), "project.tempoMap")
    if len(values) > MAXIMUM_TEMPO_EVENTS:
        raise BridgeError("SEAM tempo count exceeds the study limit")
    result: list[JsonObject] = []
    points: list[TempoPoint] = []
    for index, value in enumerate(values):
        path = f"project.tempoMap[{index}]"
        event = object_value(value, path)
        tick = integer_value(required(event, "tick", path), f"{path}.tick")
        bpm = number_value(required(event, "bpm", path), f"{path}.bpm")
        if tick < 0 or bpm <= 0 or bpm > 1000:
            raise BridgeError(f"{path} is outside USTX tempo limits")
        position = scale_tick(tick, ppq, 480, context, f"{path}.tick")
        result.append({"position": position, "bpm": bpm})
        points.append(TempoPoint(position, bpm))
    if not points or points[0].tick != 0:
        raise BridgeError("project.tempoMap must begin at tick 0")
    return result, points


def export_meters(
    root: JsonObject, ppq: int, context: ConversionContext
) -> tuple[list[JsonObject], list[MeterPoint]]:
    values = array_value(required(root, "meterMap", "project"), "project.meterMap")
    if len(values) > MAXIMUM_METER_EVENTS:
        raise BridgeError("SEAM meter count exceeds the study limit")
    result: list[JsonObject] = []
    points: list[MeterPoint] = []
    previous_tick = 0
    previous_bar = 0
    previous_numerator = 4
    previous_denominator = 4
    for index, value in enumerate(values):
        path = f"project.meterMap[{index}]"
        event = object_value(value, path)
        seam_tick = integer_value(required(event, "tick", path), f"{path}.tick")
        numerator = integer_value(
            required(event, "numerator", path), f"{path}.numerator"
        )
        denominator = integer_value(
            required(event, "denominator", path), f"{path}.denominator"
        )
        tick = scale_tick(seam_tick, ppq, 480, context, f"{path}.tick")
        if tick < previous_tick or numerator < 1 or numerator > 32:
            raise BridgeError(f"{path} is outside USTX meter limits")
        if denominator not in {1, 2, 4, 8, 16, 32}:
            raise BridgeError(f"{path}.denominator is unsupported")
        ticks_per_bar = Fraction(480 * 4 * previous_numerator, previous_denominator)
        bar_delta = rounded_fraction(
            Fraction(tick - previous_tick, 1) / ticks_per_bar,
            context,
            f"{path}.tick",
            "METER_BAR_ROUNDED",
        )
        previous_bar += bar_delta
        result.append(
            {
                "bar_position": previous_bar,
                "beat_per_bar": numerator,
                "beat_unit": denominator,
            }
        )
        points.append(MeterPoint(tick, numerator, denominator))
        previous_tick = tick
        previous_numerator = numerator
        previous_denominator = denominator
    if not result or result[0]["bar_position"] != 0:
        raise BridgeError("project.meterMap must begin at tick 0")
    return result, points
