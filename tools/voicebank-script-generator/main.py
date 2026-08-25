#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
from pathlib import Path
from typing import Any


GENERATOR_VERSION = "seam-beta-inventory-1.0.0"
SAFE_NAME = re.compile(r"^[A-Za-z0-9._/-]+$")
CSV_FIELDS = (
    "promptId",
    "takeId",
    "kind",
    "phones",
    "pronunciationHint",
    "pitchLayer",
    "sessionBlock",
    "retakeGroup",
    "filename",
)

DEFAULT_PROFILE: dict[str, Any] = {
    "profileId": "beta-ja-cvvc-v1",
    "language": "ja",
    "supportedStyles": ["original"],
    "vowels": ["a", "i", "u", "e", "o"],
    "consonants": ["k", "g", "s", "sh", "z", "j", "t", "ch", "ts", "d", "n", "h", "f", "b", "p", "m", "y", "r", "w", "v", "ky", "gy", "ny", "hy", "by", "py", "my", "ry", "fy", "vy"],
    "specialPhones": ["N", "R", "pau", "br", "cl", "glottal"],
    "includeKinds": ["sustain", "release", "breath", "glottal-attack", "special", "cv", "vc", "vv"],
    "pitchLayers": [60, 72],
    "rangeTest": {
        "method": "comfortable-range-test-v1",
        "minMidi": 60,
        "maxMidi": 72,
        "result": "PASS",
    },
    "alternateTakes": 2,
    "sessionBlockSize": 24,
    "retakeGroups": {},
}


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_json(value: Any) -> str:
    return sha256_bytes(canonical_json(value).encode("utf-8"))


def _slug(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9]+", "-", value).strip("-").lower()
    return result or "unit"


def _safe_filename(value: Any) -> bool:
    if not isinstance(value, str) or not SAFE_NAME.fullmatch(value):
        return False
    parts = value.split("/")
    return all(part not in {"", ".", ".."} for part in parts)


def _as_string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not value or any(not isinstance(item, str) or not item for item in value):
        raise ValueError(f"{label} must be a non-empty list of strings")
    if len(set(value)) != len(value):
        raise ValueError(f"{label} contains duplicate aliases")
    return list(value)


def normalize_profile(profile: dict[str, Any] | None = None) -> dict[str, Any]:
    merged = json.loads(canonical_json(DEFAULT_PROFILE))
    if profile:
        merged.update(profile)
    if not isinstance(merged.get("profileId"), str) or not merged["profileId"]:
        raise ValueError("profileId is required")
    if merged.get("language") != "ja":
        raise ValueError("language must be ja")
    merged["supportedStyles"] = _as_string_list(merged.get("supportedStyles"), "supportedStyles")
    merged["vowels"] = _as_string_list(merged.get("vowels"), "vowels")
    merged["consonants"] = _as_string_list(merged.get("consonants"), "consonants")
    merged["specialPhones"] = _as_string_list(merged.get("specialPhones"), "specialPhones")
    allowed_kinds = {"sustain", "release", "breath", "glottal-attack", "special", "cv", "vc", "vv"}
    merged["includeKinds"] = _as_string_list(merged.get("includeKinds"), "includeKinds")
    unknown = set(merged["includeKinds"]) - allowed_kinds
    if unknown:
        raise ValueError(f"includeKinds contains unsupported kinds: {sorted(unknown)}")
    layers = merged.get("pitchLayers")
    if not isinstance(layers, list) or len(layers) not in {2, 3} or any(isinstance(item, bool) or not isinstance(item, int) for item in layers):
        raise ValueError("pitchLayers must contain two or three integer MIDI layers")
    if len(set(layers)) != len(layers) or any(item < 24 or item > 96 for item in layers):
        raise ValueError("pitchLayers must be unique MIDI values between 24 and 96")
    range_test = merged.get("rangeTest")
    if not isinstance(range_test, dict):
        raise ValueError("rangeTest is required")
    for key in ("method", "result"):
        if not isinstance(range_test.get(key), str) or not range_test[key]:
            raise ValueError(f"rangeTest.{key} is required")
    if range_test.get("result") != "PASS":
        raise ValueError("rangeTest must be PASS before an inventory is generated")
    if not isinstance(range_test.get("minMidi"), int) or not isinstance(range_test.get("maxMidi"), int) or range_test["minMidi"] >= range_test["maxMidi"]:
        raise ValueError("rangeTest must define an increasing MIDI range")
    alternate = merged.get("alternateTakes")
    if not isinstance(alternate, int) or isinstance(alternate, bool) or alternate < 1 or alternate > 8:
        raise ValueError("alternateTakes must be an integer from 1 through 8")
    block_size = merged.get("sessionBlockSize")
    if not isinstance(block_size, int) or isinstance(block_size, bool) or block_size < 1:
        raise ValueError("sessionBlockSize must be a positive integer")
    if not isinstance(merged.get("retakeGroups"), dict):
        raise ValueError("retakeGroups must be an object")
    return merged


def required_sequences(profile: dict[str, Any]) -> list[dict[str, Any]]:
    vowels = profile["vowels"]
    consonants = profile["consonants"]
    special = profile["specialPhones"]
    kinds = set(profile["includeKinds"])
    sequences: list[dict[str, Any]] = []
    if "sustain" in kinds:
        sequences.extend({"kind": "sustain", "phones": [v]} for v in vowels)
    if "release" in kinds:
        sequences.extend({"kind": "release", "phones": [v, "R"]} for v in vowels)
    if "breath" in kinds:
        sequences.append({"kind": "breath", "phones": ["br"]})
    if "glottal-attack" in kinds:
        sequences.extend({"kind": "glottal-attack", "phones": ["glottal", v]} for v in vowels)
    if "special" in kinds:
        sequences.extend({"kind": "special", "phones": [phone]} for phone in special)
    if "cv" in kinds:
        sequences.extend({"kind": "cv", "phones": [consonant, vowel]} for consonant in consonants for vowel in vowels)
    if "vc" in kinds:
        sequences.extend({"kind": "vc", "phones": [vowel, consonant]} for vowel in vowels for consonant in consonants)
    if "vv" in kinds:
        sequences.extend(
            {"kind": "vv", "phones": [left, right]}
            for left in vowels
            for right in vowels
            if left != right
        )
    if not sequences:
        raise ValueError("includeKinds must produce at least one required sequence")
    return sequences


def _base_key(sequence: dict[str, Any]) -> str:
    return f"{sequence['kind']}:{':'.join(sequence['phones'])}"


def _retake_group(profile: dict[str, Any], base_key: str) -> str:
    override = profile.get("retakeGroups", {}).get(base_key)
    value = override if override is not None else f"rt-{_slug(base_key)}"
    if not isinstance(value, str) or not value or not SAFE_NAME.fullmatch(value):
        raise ValueError(f"retake group is unsafe or empty for {base_key}")
    return value


def _inventory_without_hashes(profile: dict[str, Any], units: list[dict[str, Any]], sequences: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schemaVersion": 1,
        "generatorVersion": GENERATOR_VERSION,
        "profileId": profile["profileId"],
        "language": profile["language"],
        "supportedStyles": profile["supportedStyles"],
        "vowels": profile["vowels"],
        "consonantFamilies": profile["consonants"],
        "specialPhones": profile["specialPhones"],
        "includeKinds": profile["includeKinds"],
        "pitchLayers": profile["pitchLayers"],
        "rangeTest": profile["rangeTest"],
        "requiredCoverage": [_base_key(sequence) for sequence in sequences],
        "units": units,
    }


def render_operator_csv(inventory: dict[str, Any]) -> str:
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=CSV_FIELDS, extrasaction="ignore", lineterminator="\n")
    writer.writeheader()
    for unit in inventory.get("units", []):
        row = dict(unit)
        row["phones"] = " ".join(unit["phones"])
        writer.writerow(row)
    return output.getvalue()


def validate_inventory(inventory: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if not isinstance(inventory, dict):
        return ["inventory must be an object"]
    for key in ("schemaVersion", "generatorVersion", "profileId", "language", "requiredCoverage", "units", "inventorySha256", "scriptSha256"):
        if key not in inventory:
            errors.append(f"inventory.{key} is required")
    if inventory.get("schemaVersion") != 1:
        errors.append("inventory.schemaVersion must be 1")
    if inventory.get("generatorVersion") != GENERATOR_VERSION:
        errors.append("inventory.generatorVersion does not match this generator")
    if inventory.get("language") != "ja":
        errors.append("inventory.language must be ja")
    coverage = inventory.get("requiredCoverage")
    units = inventory.get("units")
    if not isinstance(coverage, list) or not coverage or any(not isinstance(item, str) for item in coverage):
        errors.append("requiredCoverage must be a non-empty list of strings")
        coverage = []
    if len(set(coverage)) != len(coverage):
        errors.append("requiredCoverage contains duplicate aliases")
    if not isinstance(units, list) or not units:
        errors.append("units must be a non-empty list")
        units = []
    ids: set[str] = set()
    filenames: set[str] = set()
    seen_coverage: set[str] = set()
    layers = set(inventory.get("pitchLayers", []))
    for index, unit in enumerate(units):
        label = f"units[{index}]"
        if not isinstance(unit, dict):
            errors.append(f"{label} must be an object")
            continue
        for key in ("promptId", "takeId", "kind", "phones", "pronunciationHint", "pitchLayer", "sessionBlock", "retakeGroup", "filename", "coverageKey"):
            if key not in unit:
                errors.append(f"{label}.{key} is required")
        prompt_id = unit.get("promptId")
        take_id = unit.get("takeId")
        if not isinstance(prompt_id, str) or prompt_id in ids:
            errors.append(f"{label}.promptId is missing or duplicated")
        elif prompt_id:
            ids.add(prompt_id)
        if not isinstance(take_id, str) or not take_id:
            errors.append(f"{label}.takeId is required")
        if not isinstance(unit.get("phones"), list) or not unit["phones"] or any(not isinstance(phone, str) or not phone for phone in unit["phones"]):
            errors.append(f"{label}.phones must be a non-empty string list")
        if unit.get("pitchLayer") not in layers:
            errors.append(f"{label}.pitchLayer is not assigned to an inventory layer")
        if not isinstance(unit.get("sessionBlock"), int) or unit.get("sessionBlock", 0) < 1:
            errors.append(f"{label}.sessionBlock must be positive")
        if not isinstance(unit.get("retakeGroup"), str) or not unit["retakeGroup"]:
            errors.append(f"{label}.retakeGroup is required")
        filename = unit.get("filename")
        if not _safe_filename(filename):
            errors.append(f"{label}.filename is unsafe")
        elif filename in filenames:
            errors.append(f"{label}.filename is duplicated")
        else:
            filenames.add(filename)
        coverage_key = unit.get("coverageKey")
        if coverage_key not in coverage:
            errors.append(f"{label}.coverageKey is not declared")
        elif unit.get("pitchLayer") == min(layers) if layers else False:
            seen_coverage.add(coverage_key)
    missing = sorted(set(coverage) - seen_coverage)
    errors.extend(f"required coverage is missing: {item}" for item in missing)
    if isinstance(inventory.get("inventorySha256"), str):
        payload = {key: inventory.get(key) for key in ("schemaVersion", "generatorVersion", "profileId", "language", "supportedStyles", "vowels", "consonantFamilies", "specialPhones", "includeKinds", "pitchLayers", "rangeTest", "requiredCoverage", "units")}
        if sha256_json(payload) != inventory["inventorySha256"]:
            errors.append("inventorySha256 does not match canonical inventory content")
    if isinstance(inventory.get("scriptSha256"), str) and sha256_bytes(render_operator_csv(inventory).encode("utf-8")) != inventory["scriptSha256"]:
        errors.append("scriptSha256 does not match deterministic CSV content")
    return errors


def generate_inventory(profile: dict[str, Any] | None = None) -> dict[str, Any]:
    normalized = normalize_profile(profile)
    sequences = required_sequences(normalized)
    units: list[dict[str, Any]] = []
    sequence_index = 0
    for sequence in sequences:
        coverage_key = _base_key(sequence)
        retake_group = _retake_group(normalized, coverage_key)
        for layer in normalized["pitchLayers"]:
            for alternate in range(1, normalized["alternateTakes"] + 1):
                sequence_index += 1
                prompt_id = f"P{sequence_index:05d}-{_slug(coverage_key)}-p{layer}"
                take_id = f"{prompt_id}-t{alternate:02d}"
                filename = f"takes/p{layer}/{take_id}.wav"
                units.append(
                    {
                        "promptId": prompt_id,
                        "takeId": take_id,
                        "kind": sequence["kind"],
                        "phones": list(sequence["phones"]),
                        "pronunciationHint": " ".join(sequence["phones"]),
                        "pitchLayer": layer,
                        "sessionBlock": 1 + (sequence_index - 1) // normalized["sessionBlockSize"],
                        "retakeGroup": retake_group,
                        "filename": filename,
                        "coverageKey": coverage_key,
                    }
                )
    payload = _inventory_without_hashes(normalized, units, sequences)
    inventory = dict(payload)
    inventory["inventorySha256"] = sha256_json(payload)
    inventory["scriptSha256"] = sha256_bytes(render_operator_csv(inventory).encode("utf-8"))
    errors = validate_inventory(inventory)
    if errors:
        raise ValueError("generated inventory is invalid: " + "; ".join(errors))
    return inventory


def _load_profile(path: Path | None) -> dict[str, Any]:
    if path is None:
        return dict(DEFAULT_PROFILE)
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("profile JSON root must be an object")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate deterministic Project SEAM Beta Voicebank recording scripts")
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--csv-output", type=Path)
    args = parser.parse_args(argv)
    try:
        inventory = generate_inventory(_load_profile(args.profile))
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        parser.error(str(exc))
    json_text = json.dumps(inventory, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
    csv_text = render_operator_csv(inventory)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json_text, encoding="utf-8", newline="\n")
    if args.csv_output:
        args.csv_output.parent.mkdir(parents=True, exist_ok=True)
        args.csv_output.write_text(csv_text, encoding="utf-8", newline="\n")
    if not args.json_output and not args.csv_output:
        print(json_text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
