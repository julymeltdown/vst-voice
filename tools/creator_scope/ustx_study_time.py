from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
import math

from .ustx_study_contracts import BridgeError, ConversionContext


@dataclass(frozen=True, slots=True)
class TempoPoint:
    tick: int
    bpm: float


@dataclass(frozen=True, slots=True)
class MeterPoint:
    tick: int
    numerator: int
    denominator: int


def next_beat_tick(tick: int, meters: list[MeterPoint]) -> int:
    if tick < 0 or not meters or meters[0].tick != 0:
        raise BridgeError("meter map cannot resolve the next beat")
    active = meters[0]
    for point in meters[1:]:
        if point.tick > tick:
            break
        active = point
    beat_ticks = 480 * 4 // active.denominator
    offset = tick - active.tick
    return active.tick + (offset // beat_ticks + 1) * beat_ticks


def rounded_fraction(
    value: Fraction,
    context: ConversionContext,
    path: str,
    code: str,
) -> int:
    if value.denominator == 1:
        return value.numerator
    if value < 0:
        rounded = -math.floor(float(-value) + 0.5)
    else:
        rounded = math.floor(float(value) + 0.5)
    context.loss(code, path, f"rounded {float(value):.9g} to {rounded}")
    return rounded


def scale_tick(
    tick: int,
    source_ppq: int,
    target_ppq: int,
    context: ConversionContext,
    path: str,
) -> int:
    if tick < 0:
        raise BridgeError(f"{path} must not be negative")
    return rounded_fraction(
        Fraction(tick * target_ppq, source_ppq),
        context,
        path,
        "TICK_ROUNDED",
    )


def tick_to_milliseconds(tick: float, tempos: list[TempoPoint], ppq: int) -> float:
    if tick < 0:
        raise BridgeError("pitch position resolves before project start")
    milliseconds = 0.0
    cursor = 0.0
    bpm = tempos[0].bpm
    for point in tempos[1:]:
        if point.tick >= tick:
            break
        milliseconds += (point.tick - cursor) * 60_000.0 / (bpm * ppq)
        cursor = float(point.tick)
        bpm = point.bpm
    return milliseconds + (tick - cursor) * 60_000.0 / (bpm * ppq)


def milliseconds_to_tick(
    milliseconds: float, tempos: list[TempoPoint], ppq: int
) -> float:
    if milliseconds < 0:
        raise BridgeError("pitch position resolves before project start")
    remaining = milliseconds
    cursor = 0.0
    bpm = tempos[0].bpm
    for point in tempos[1:]:
        segment = (point.tick - cursor) * 60_000.0 / (bpm * ppq)
        if remaining <= segment:
            return cursor + remaining * bpm * ppq / 60_000.0
        remaining -= segment
        cursor = float(point.tick)
        bpm = point.bpm
    return cursor + remaining * bpm * ppq / 60_000.0


def pitch_milliseconds_to_tick(
    note_tick: int,
    offset_ms: float,
    tempos: list[TempoPoint],
    ppq: int,
    context: ConversionContext,
    path: str,
) -> int:
    start_ms = tick_to_milliseconds(float(note_tick), tempos, ppq)
    target = milliseconds_to_tick(start_ms + offset_ms, tempos, ppq)
    return rounded_fraction(Fraction(str(target)), context, path, "PITCH_TIME_ROUNDED")


def pitch_tick_to_milliseconds(
    note_tick: int, point_tick: int, tempos: list[TempoPoint], ppq: int
) -> float:
    start = tick_to_milliseconds(float(note_tick), tempos, ppq)
    point = tick_to_milliseconds(float(point_tick), tempos, ppq)
    return point - start
