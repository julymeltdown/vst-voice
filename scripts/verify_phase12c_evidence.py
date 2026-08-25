#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path
from typing import Any


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")


class EvidenceError(ValueError):
    pass


def read_json(path: Path) -> dict[str, Any]:
    if path.stat().st_size > 4 * 1024 * 1024:
        raise EvidenceError(f"evidence file exceeds 4 MiB: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise EvidenceError(f"evidence root must be an object: {path}")
    return value


def validate_live_summary(summary: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if summary.get("result") != "PASS":
        errors.append("live summary result must be PASS")
    if summary.get("finite") is not True:
        errors.append("live summary finite must be true")
    if not isinstance(summary.get("voicebankId"), str) or not summary["voicebankId"]:
        errors.append("live summary voicebankId is required")
    if not isinstance(summary.get("voicebankVersion"), str) or not summary["voicebankVersion"]:
        errors.append("live summary voicebankVersion is required")
    if not isinstance(summary.get("voicebankContentHash"), str) or HEX64.fullmatch(summary["voicebankContentHash"]) is None:
        errors.append("live summary voicebankContentHash must be SHA-256")
    for key in ("unitCount", "renderedFrames", "noteOns", "steals", "transitionFallbacks", "eventOverflows"):
        if not isinstance(summary.get(key), int) or summary[key] < 0:
            errors.append(f"live summary {key} must be a non-negative integer")
    if not isinstance(summary.get("energy"), (int, float)) or summary["energy"] <= 1.0:
        errors.append("live summary energy must be greater than 1")
    if summary.get("renderedFrames", 0) == 0:
        errors.append("live summary must render at least one frame")
    return errors


def validate_validator_result(result: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if result.get("status") != "PASS":
        errors.append("validator result must be PASS")
    if not isinstance(result.get("pluginSha256"), str) or HEX64.fullmatch(result["pluginSha256"]) is None:
        errors.append("validator result pluginSha256 must be SHA-256")
    if not isinstance(result.get("rawLogSha256"), str) or HEX64.fullmatch(result["rawLogSha256"]) is None:
        errors.append("validator result rawLogSha256 must be SHA-256")
    return errors


def validate_soak_result(result: dict[str, Any], require_full: bool) -> list[str]:
    errors: list[str] = []
    profile = result.get("profile")
    if not isinstance(profile, str) or profile not in {"smoke", "full"}:
        errors.append("soak profile must be smoke or full")
    if result.get("result") != "PASS":
        errors.append("soak result must be PASS")
    if require_full and profile != "full":
        errors.append("full evidence requires the full soak profile")
    required = 7200 if profile == "full" else 5
    if result.get("requiredSeconds") != required:
        errors.append(f"soak requiredSeconds must be {required}")
    if (
        not isinstance(result.get("elapsedSeconds"), int)
        or isinstance(result["elapsedSeconds"], bool)
        or result["elapsedSeconds"] < required
    ):
        errors.append(f"soak elapsedSeconds must be at least {required}")
    if (
        not isinstance(result.get("blocks"), int)
        or isinstance(result["blocks"], bool)
        or result["blocks"] <= 0
    ):
        errors.append("soak blocks must be positive")
    for key in (
        "eventBlocks",
        "resourcePublishes",
        "resourceClears",
        "maxActiveVoices",
        "noteOns",
        "noteOffs",
        "steals",
        "transitionHits",
        "transitionFallbacks",
        "midiEvents",
        "expressionEvents",
        "renderedFrames",
        "silentFramesNoResource",
        "eventOverflows",
    ):
        value = result.get(key)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            errors.append(f"soak {key} must be a non-negative integer")
    if result.get("finite") is not True:
        errors.append("soak finite must be true")
    for key in ("absoluteEnergy", "peak"):
        value = result.get(key)
        if (
            not isinstance(value, (int, float))
            or isinstance(value, bool)
            or not math.isfinite(value)
            or value <= 0.0
        ):
            errors.append(f"soak {key} must be finite and positive")
    if isinstance(result.get("maxActiveVoices"), int) and result["maxActiveVoices"] > 32:
        errors.append("soak maxActiveVoices must not exceed 32")
    for key in (
        "eventBlocks",
        "resourcePublishes",
        "resourceClears",
        "noteOns",
        "noteOffs",
        "steals",
        "transitionHits",
        "midiEvents",
        "expressionEvents",
        "renderedFrames",
    ):
        if isinstance(result.get(key), int) and result[key] <= 0:
            errors.append(f"soak {key} must be positive")
    if isinstance(result.get("eventOverflows"), int) and result["eventOverflows"] != 0:
        errors.append("soak eventOverflows must be zero")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--validator-result", type=Path)
    parser.add_argument("--soak-result", type=Path)
    parser.add_argument("--require-full", action="store_true")
    args = parser.parse_args()
    try:
        errors = validate_live_summary(read_json(args.summary))
        if args.validator_result:
            errors.extend(validate_validator_result(read_json(args.validator_result)))
        if args.soak_result:
            errors.extend(validate_soak_result(read_json(args.soak_result), args.require_full))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        errors = [str(exc)]
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("PHASE12C_EVIDENCE=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
