from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import PurePosixPath
import re
from typing import Final, TypeAlias

JsonValue: TypeAlias = None | bool | int | float | str | list["JsonValue"] | dict[str, "JsonValue"]
SHA256_PATTERN: Final = re.compile(r"[0-9a-f]{64}")
ID_PATTERN: Final = re.compile(r"[a-z0-9][a-z0-9-]{0,63}")


@dataclass(frozen=True, slots=True)
class CorpusError(Exception):
    code: str
    detail: str

    def __str__(self) -> str:
        return f"{self.code}: {self.detail}"


@dataclass(frozen=True, slots=True)
class JsonObject:
    fields: dict[str, JsonValue]

    def string(self, key: str) -> str:
        value = self.fields.get(key)
        if not isinstance(value, str) or not value:
            raise CorpusError("contract_shape", f"{key} must be a nonempty string")
        return value

    def array(self, key: str) -> list[JsonValue]:
        value = self.fields.get(key)
        if not isinstance(value, list) or not value:
            raise CorpusError("contract_shape", f"{key} must be a nonempty array")
        return value


def parse_object(payload: bytes) -> JsonObject:
    def pairs(entries: list[tuple[str, JsonValue]]) -> dict[str, JsonValue]:
        fields: dict[str, JsonValue] = {}
        for key, value in entries:
            if key in fields:
                raise CorpusError("contract_shape", f"duplicate key: {key}")
            fields[key] = value
        return fields

    try:
        value: JsonValue = json.loads(payload, object_pairs_hook=pairs)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise CorpusError("contract_json", str(error)) from error
    return object_value(value)


def object_value(value: JsonValue) -> JsonObject:
    if not isinstance(value, dict):
        raise CorpusError("contract_shape", "expected a JSON object")
    return JsonObject(value)


def relative_path(value: str) -> str:
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or "\\" in value or ":" in value
            or any(part in {"", ".", ".."} for part in value.split("/"))
            or any(ord(character) < 32 for character in value)):
        raise CorpusError("asset_path", value)
    return value


@dataclass(frozen=True, slots=True)
class AssetLock:
    path: str
    sha256: str


@dataclass(frozen=True, slots=True)
class CorpusCase:
    id: str
    project: str


@dataclass(frozen=True, slots=True)
class CorpusContract:
    id: str
    bank_root: str
    manifest: str
    provenance: str
    notice: str
    bank_readme: str
    source: str
    melody_notice: str
    cases: tuple[CorpusCase, ...]
    assets: tuple[AssetLock, ...]

    @classmethod
    def parse(cls, payload: bytes) -> CorpusContract:
        root = parse_object(payload)
        if (type(root.fields.get("schema_version")) is not int
                or root.fields.get("schema_version") != 1
                or root.string("purpose") != "diagnostic"):
            raise CorpusError("contract_schema", "expected diagnostic corpus schema 1")
        identifier = root.string("id")
        if ID_PATTERN.fullmatch(identifier) is None:
            raise CorpusError("contract_shape", "invalid corpus id")
        assets = []
        for item in root.array("assets"):
            entry = object_value(item)
            digest = entry.string("sha256")
            if SHA256_PATTERN.fullmatch(digest) is None:
                raise CorpusError("contract_hash", entry.string("path"))
            assets.append(AssetLock(relative_path(entry.string("path")), digest))
        if len(assets) > 128 or len({asset.path.casefold() for asset in assets}) != len(assets):
            raise CorpusError("contract_shape", "duplicate or excessive assets")
        cases = []
        for item in root.array("cases"):
            entry = object_value(item)
            name = entry.string("id")
            if ID_PATTERN.fullmatch(name) is None:
                raise CorpusError("contract_shape", "invalid case id")
            cases.append(CorpusCase(name, relative_path(entry.string("project"))))
        if len(cases) > 8 or len({case.id for case in cases}) != len(cases):
            raise CorpusError("contract_shape", "duplicate or excessive cases")
        result = cls(identifier, *(relative_path(root.string(key)) for key in (
            "bank_root", "manifest", "provenance", "notice", "bank_readme",
            "source", "melody_notice")), tuple(cases), tuple(assets))
        locked = {asset.path for asset in assets}
        required = {result.manifest, result.provenance, result.notice, result.bank_readme,
                    result.source, result.melody_notice, *(case.project for case in cases)}
        if not required.issubset(locked):
            raise CorpusError("contract_lock", "every required input needs a hash")
        bank_prefix = result.bank_root + "/"
        if any(not path.startswith(bank_prefix) for path in (
                result.manifest, result.provenance, result.notice, result.bank_readme)):
            raise CorpusError("contract_lock", "bank evidence must be inside bank_root")
        if str(PurePosixPath(result.manifest).parent) != result.bank_root:
            raise CorpusError("contract_lock", "manifest must be directly inside bank_root")
        return result
