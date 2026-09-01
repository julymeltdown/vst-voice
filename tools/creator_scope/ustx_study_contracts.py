from __future__ import annotations

import math
from typing import Final

from .evidence import JsonObject, JsonValue


STUDY_BRIDGE_VERSION: Final = "0.1.0-study"
MAXIMUM_INPUT_BYTES: Final = 4 * 1024 * 1024
MAXIMUM_DEPTH: Final = 32
MAXIMUM_NODES: Final = 100_000
MAXIMUM_COLLECTION_ENTRIES: Final = 50_000
MAXIMUM_SCALAR_BYTES: Final = 1 * 1024 * 1024
MAXIMUM_TRACKS: Final = 16
MAXIMUM_PARTS: Final = 256
MAXIMUM_NOTES: Final = 50_000
MAXIMUM_TEMPO_EVENTS: Final = 256
MAXIMUM_METER_EVENTS: Final = 256


class BridgeError(ValueError):
    pass


class ConversionContext:
    __slots__ = ("direction", "losses")

    def __init__(self, direction: str) -> None:
        self.direction = direction
        self.losses: list[JsonObject] = []

    def loss(self, code: str, path: str, detail: str) -> None:
        self.losses.append({"code": code, "path": path, "detail": detail})


class IdAllocator:
    __slots__ = ("next_value",)

    def __init__(self) -> None:
        self.next_value = 2

    def allocate(self) -> str:
        value = f"{self.next_value:x}"
        self.next_value += 1
        return value


def object_value(value: JsonValue, path: str) -> JsonObject:
    if not isinstance(value, dict):
        raise BridgeError(f"{path} must be an object")
    return value


def array_value(value: JsonValue, path: str) -> list[JsonValue]:
    if not isinstance(value, list):
        raise BridgeError(f"{path} must be an array")
    return value


def string_value(value: JsonValue, path: str) -> str:
    if not isinstance(value, str):
        raise BridgeError(f"{path} must be a string")
    return value


def integer_value(value: JsonValue, path: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise BridgeError(f"{path} must be an integer")
    return value


def number_value(value: JsonValue, path: str) -> float:
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise BridgeError(f"{path} must be a number")
    try:
        result = float(value)
    except OverflowError as error:
        raise BridgeError(f"{path} must be finite") from error
    if not math.isfinite(result):
        raise BridgeError(f"{path} must be finite")
    return result


def boolean_value(value: JsonValue, path: str) -> bool:
    if not isinstance(value, bool):
        raise BridgeError(f"{path} must be a boolean")
    return value


def required(value: JsonObject, key: str, path: str) -> JsonValue:
    if key not in value:
        raise BridgeError(f"{path}.{key} is required")
    return value[key]


def optional_string(value: JsonObject, key: str, default: str, path: str) -> str:
    item = value.get(key)
    return default if item is None else string_value(item, f"{path}.{key}")


def optional_number(value: JsonObject, key: str, default: float, path: str) -> float:
    item = value.get(key)
    return default if item is None else number_value(item, f"{path}.{key}")


def optional_boolean(value: JsonObject, key: str, default: bool, path: str) -> bool:
    item = value.get(key)
    return default if item is None else boolean_value(item, f"{path}.{key}")
