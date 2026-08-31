from __future__ import annotations

import csv
import hashlib
import io
import json
from typing import Any

from .profile import (
    CSV_FIELDS,
    GENERATOR_VERSION,
    base_key,
    normalize_profile,
    required_sequences,
    retake_group,
    safe_filename,
    slug,
)


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_json(value: Any) -> str:
    return sha256_bytes(canonical_json(value).encode("utf-8"))


def _inventory_payload(
    profile: dict[str, Any], units: list[dict[str, Any]], sequences: list[dict[str, Any]]
) -> dict[str, Any]:
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
        "requiredCoverage": [base_key(sequence) for sequence in sequences],
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
    required = (
        "schemaVersion", "generatorVersion", "profileId", "language",
        "requiredCoverage", "units", "inventorySha256", "scriptSha256",
    )
    for key in required:
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
    prompt_ids: set[str] = set()
    take_ids: set[str] = set()
    filenames: set[str] = set()
    seen_pairs: set[tuple[str, int]] = set()
    pitch_layers = inventory.get("pitchLayers")
    if (
        not isinstance(pitch_layers, list)
        or len(pitch_layers) not in {2, 3}
        or any(isinstance(item, bool) or not isinstance(item, int) for item in pitch_layers)
        or len(set(pitch_layers)) != len(pitch_layers)
    ):
        errors.append("pitchLayers must contain two or three unique integer MIDI layers")
        layers: set[int] = set()
    else:
        layers = set(pitch_layers)
    for index, unit in enumerate(units):
        label = f"units[{index}]"
        if not isinstance(unit, dict):
            errors.append(f"{label} must be an object")
            continue
        unit_required = (
            "promptId", "takeId", "kind", "phones", "pronunciationHint",
            "pitchLayer", "sessionBlock", "retakeGroup", "filename", "coverageKey",
        )
        for key in unit_required:
            if key not in unit:
                errors.append(f"{label}.{key} is required")
        prompt_id = unit.get("promptId")
        if not isinstance(prompt_id, str) or not prompt_id or prompt_id in prompt_ids:
            errors.append(f"{label}.promptId is missing or duplicated")
        else:
            prompt_ids.add(prompt_id)
        take_id = unit.get("takeId")
        if not isinstance(take_id, str) or not take_id or take_id in take_ids:
            errors.append(f"{label}.takeId is missing or duplicated")
        else:
            take_ids.add(take_id)
        phones = unit.get("phones")
        if not isinstance(phones, list) or not phones or any(not isinstance(phone, str) or not phone for phone in phones):
            errors.append(f"{label}.phones must be a non-empty string list")
        if unit.get("pitchLayer") not in layers:
            errors.append(f"{label}.pitchLayer is not assigned to an inventory layer")
        if not isinstance(unit.get("sessionBlock"), int) or unit.get("sessionBlock", 0) < 1:
            errors.append(f"{label}.sessionBlock must be positive")
        if not isinstance(unit.get("retakeGroup"), str) or not unit.get("retakeGroup"):
            errors.append(f"{label}.retakeGroup is required")
        filename = unit.get("filename")
        if not safe_filename(filename):
            errors.append(f"{label}.filename is unsafe")
        elif filename in filenames:
            errors.append(f"{label}.filename is duplicated")
        else:
            filenames.add(filename)
        coverage_key = unit.get("coverageKey")
        if coverage_key not in coverage:
            errors.append(f"{label}.coverageKey is not declared")
        elif unit.get("pitchLayer") in layers:
            seen_pairs.add((coverage_key, unit["pitchLayer"]))
    expected_pairs = {
        (coverage_key, layer) for coverage_key in coverage for layer in layers
    }
    errors.extend(
        f"required coverage is missing: {coverage_key} at pitch layer {layer}"
        for coverage_key, layer in sorted(expected_pairs - seen_pairs)
    )
    if isinstance(inventory.get("inventorySha256"), str):
        payload_keys = (
            "schemaVersion", "generatorVersion", "profileId", "language", "supportedStyles",
            "vowels", "consonantFamilies", "specialPhones", "includeKinds", "pitchLayers",
            "rangeTest", "requiredCoverage", "units",
        )
        payload = {key: inventory.get(key) for key in payload_keys}
        if sha256_json(payload) != inventory["inventorySha256"]:
            errors.append("inventorySha256 does not match canonical inventory content")
    if (
        isinstance(inventory.get("scriptSha256"), str)
        and sha256_bytes(render_operator_csv(inventory).encode("utf-8")) != inventory["scriptSha256"]
    ):
        errors.append("scriptSha256 does not match deterministic CSV content")
    return errors


def generate_inventory(profile: dict[str, Any] | None = None) -> dict[str, Any]:
    normalized = normalize_profile(profile)
    sequences = required_sequences(normalized)
    units: list[dict[str, Any]] = []
    sequence_index = 0
    for sequence in sequences:
        coverage_key = base_key(sequence)
        group = retake_group(normalized, coverage_key)
        for layer in normalized["pitchLayers"]:
            for alternate in range(1, normalized["alternateTakes"] + 1):
                sequence_index += 1
                prompt_id = f"P{sequence_index:05d}-{slug(coverage_key)}-p{layer}"
                take_id = f"{prompt_id}-t{alternate:02d}"
                units.append({
                    "promptId": prompt_id,
                    "takeId": take_id,
                    "kind": sequence["kind"],
                    "phones": list(sequence["phones"]),
                    "pronunciationHint": " ".join(sequence["phones"]),
                    "pitchLayer": layer,
                    "sessionBlock": 1 + (sequence_index - 1) // normalized["sessionBlockSize"],
                    "retakeGroup": group,
                    "filename": f"takes/p{layer}/{take_id}.wav",
                    "coverageKey": coverage_key,
                })
    payload = _inventory_payload(normalized, units, sequences)
    inventory = dict(payload)
    inventory["inventorySha256"] = sha256_json(payload)
    inventory["scriptSha256"] = sha256_bytes(render_operator_csv(inventory).encode("utf-8"))
    errors = validate_inventory(inventory)
    if errors:
        raise ValueError("generated inventory is invalid: " + "; ".join(errors))
    return inventory


def production_assignments(inventory: dict[str, Any]) -> list[dict[str, Any]]:
    errors = validate_inventory(inventory)
    if errors:
        raise ValueError("production assignments require a valid inventory: " + "; ".join(errors))
    planned: dict[tuple[str, int], dict[str, Any]] = {}
    for unit in inventory["units"]:
        key = (unit["coverageKey"], unit["pitchLayer"])
        planned.setdefault(
            key,
            {
                "coverageKey": unit["coverageKey"],
                "pitchLayer": unit["pitchLayer"],
                "promptId": unit["promptId"],
                "plannedTakeId": unit["takeId"],
                "takeId": "",
                "state": "MISSING",
                "markerReviewed": False,
                "pitchReviewed": False,
            },
        )
    expected = [
        (coverage, layer)
        for coverage in inventory["requiredCoverage"]
        for layer in inventory["pitchLayers"]
    ]
    missing = [key for key in expected if key not in planned]
    if missing:
        raise ValueError(f"production assignments are missing required inventory pairs: {missing}")
    return [planned[key] for key in expected]
