from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
ALLOWED_SESSION_STATUS = {"NOT_RUN", "IN_PROGRESS", "COMPLETE", "FAIL"}
REQUIRED_QA_FIELDS = ("clipping", "dcOffset", "silence", "rootPitch", "markerOrder", "pitchMarks")


@dataclass(frozen=True, slots=True)
class ProductionResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors), "blocked": list(self.blocked)}


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_hex_digest(value: Any) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def is_timestamp(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
        return True
    except ValueError:
        return False


def inventory_index(
    inventory: dict[str, Any], errors: list[str]
) -> tuple[dict[str, dict[str, Any]], set[str], set[int]]:
    units = inventory.get("units")
    coverage = inventory.get("requiredCoverage")
    layers = inventory.get("pitchLayers")
    if not isinstance(units, list) or not isinstance(coverage, list) or not isinstance(layers, list):
        errors.append("inventory must contain units, requiredCoverage, and pitchLayers")
        return {}, set(), set()
    by_take: dict[str, dict[str, Any]] = {}
    for index, unit in enumerate(units):
        if not isinstance(unit, dict):
            errors.append(f"inventory.units[{index}] must be an object")
            continue
        take_id = unit.get("takeId")
        if not isinstance(take_id, str) or not take_id or take_id in by_take:
            errors.append(f"inventory takeId is missing or duplicated at index {index}")
            continue
        by_take[take_id] = unit
    return by_take, {item for item in coverage if isinstance(item, str)}, {
        item for item in layers if isinstance(item, int)
    }


def check_artifact(root: Path, item: dict[str, Any], label: str, errors: list[str]) -> None:
    path_text = item.get("path")
    if not isinstance(path_text, str) or not path_text or "\\" in path_text:
        errors.append(f"{label}.path must be a safe relative path")
        return
    relative = PurePosixPath(path_text)
    if relative.is_absolute() or any(part in {"", ".", ".."} for part in relative.parts):
        errors.append(f"{label}.path must be a safe relative path")
        return
    if not is_hex_digest(item.get("sha256")):
        errors.append(f"{label}.sha256 must be a 64-character hexadecimal digest")
    candidate = root / path_text
    try:
        resolved_root = root.resolve(strict=True)
        if candidate.is_symlink():
            errors.append(f"{label}.path must not be a symbolic link")
            return
        resolved = candidate.resolve(strict=True)
        if resolved_root != resolved and resolved_root not in resolved.parents:
            errors.append(f"{label}.path escapes the recording root")
            return
        if not resolved.is_file():
            errors.append(f"{label}.path is not a regular file")
            return
        if is_hex_digest(item.get("sha256")) and sha256_file(resolved) != item["sha256"].lower():
            errors.append(f"{label}.sha256 does not match file bytes")
    except FileNotFoundError:
        errors.append(f"{label}.path does not exist")
    except OSError as exc:
        errors.append(f"{label}.path cannot be inspected: {exc}")


def take_quality_errors(take: dict[str, Any], label: str) -> list[str]:
    errors: list[str] = []
    quality = take.get("quality")
    if not isinstance(quality, dict):
        return [f"{label}.quality is required"]
    for key in REQUIRED_QA_FIELDS:
        if quality.get(key) != "PASS":
            errors.append(f"{label}.quality.{key} must be PASS")
    for key in ("rootMidi", "analyzedMidi"):
        if not isinstance(quality.get(key), int):
            errors.append(f"{label}.quality.{key} must be an integer")
    if (
        isinstance(quality.get("rootMidi"), int)
        and isinstance(quality.get("analyzedMidi"), int)
        and abs(quality["rootMidi"] - quality["analyzedMidi"]) > 1
    ):
        errors.append(f"{label}.quality root-pitch octave error")
    return errors


def accepted_takes(sessions: Iterable[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    accepted: dict[str, dict[str, Any]] = {}
    for session in sessions:
        takes = session.get("takes", []) if isinstance(session, dict) else []
        for take in takes if isinstance(takes, list) else []:
            if isinstance(take, dict) and take.get("status") == "ACCEPTED" and isinstance(take.get("takeId"), str):
                accepted[take["takeId"]] = take
    return accepted
