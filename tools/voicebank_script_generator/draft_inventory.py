"""Style-owned draft inventories; generation is not range qualification.

Schema 1 and its hashes remain owned by inventory.py. This explicit schema-2
entry point must not be passed to a legacy style-free producer workspace.
"""
from __future__ import annotations

import csv
import hashlib
import io
import json
import re
from typing import Any

from .profile import DEFAULT_PROFILE, base_key, required_sequences

VERSION = "seam-draft-inventory-2.0.0"
PROFILE_FIELDS = (
    "profileId", "language", "supportedStyles", "vowels", "consonants",
    "specialPhones", "includeKinds", "pitchLayers", "requestedRange",
    "alternateTakes", "sessionBlockSize",
)
CSV_FIELDS = (
    "promptId", "takeId", "language", "style", "kind", "phones",
    "pronunciationHint", "pitchLayer", "sessionBlock", "retakeGroup", "filename",
)
MAXIMUM_UNITS = 16384


def _json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _text(value: Any) -> bool:
    return isinstance(value, str) and 0 < len(value) <= 128 and not any(
        ord(c) < 32 or ord(c) == 127 or 0xD800 <= ord(c) <= 0xDFFF for c in value
    ) and len(value.encode("utf-8")) <= 128


def normalize_draft_profile(profile: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(profile, dict) or set(profile) - set(PROFILE_FIELDS):
        raise ValueError("draft profile contains unknown fields; rangeTest/PASS is not draft authority")
    result = {key: json.loads(_json(DEFAULT_PROFILE[key])) for key in PROFILE_FIELDS
              if key in DEFAULT_PROFILE and key not in {"profileId", "pitchLayers"}}
    result.update({"supportedStyles": ["neutral"], "pitchLayers": [60, 66, 72],
                   "requestedRange": {"minMidi": 60, "maxMidi": 72}})
    result.update(profile)
    if not _text(result.get("profileId")):
        raise ValueError("draft profileId is required and bounded")
    if result.get("language") != "ja":
        raise ValueError("draft language profile is not implemented; Japanese rules cannot stand in for other languages")
    for field in ("supportedStyles", "vowels", "consonants", "specialPhones", "includeKinds"):
        values = result.get(field)
        limit = 16 if field == "supportedStyles" else 64
        if not isinstance(values, list) or not 1 <= len(values) <= limit or not all(_text(v) for v in values):
            raise ValueError(f"{field} must contain bounded nonempty symbols")
        if len(set(values)) != len(values):
            raise ValueError(f"{field} contains duplicate symbols")
        if field in {"vowels", "consonants", "specialPhones"} and any(
            not re.fullmatch(r"[A-Za-z0-9_@+.-]+", value) for value in values
        ):
            raise ValueError(f"{field} contains an unsupported phonetic symbol")
    if set(result["includeKinds"]) - set(DEFAULT_PROFILE["includeKinds"]):
        raise ValueError("includeKinds contains unsupported kinds")
    layers = result.get("pitchLayers")
    if not isinstance(layers, list) or len(layers) not in (2, 3) or any(
        type(value) is not int or not 24 <= value <= 96 for value in layers
    ) or len(set(layers)) != len(layers):
        raise ValueError("pitchLayers requires two or three distinct bounded MIDI values")
    requested = result.get("requestedRange")
    if not isinstance(requested, dict) or set(requested) != {"minMidi", "maxMidi"} or any(
        type(requested[key]) is not int for key in ("minMidi", "maxMidi")
    ) or not 24 <= requested["minMidi"] < requested["maxMidi"] <= 96:
        raise ValueError("requestedRange must contain increasing bounded MIDI values")
    if any(not requested["minMidi"] <= layer <= requested["maxMidi"] for layer in layers):
        raise ValueError("pitch layers must lie inside the requested, unassessed range")
    for field, maximum in (("alternateTakes", 8), ("sessionBlockSize", 1024)):
        if type(result.get(field)) is not int or not 1 <= result[field] <= maximum:
            raise ValueError(f"{field} exceeds bounds")
    sequences = required_sequences(result)
    if len({base_key(s) for s in sequences}) != len(sequences):
        raise ValueError("phone symbols produce ambiguous coverage keys")
    count = len(sequences) * len(layers) * len(result["supportedStyles"]) * result["alternateTakes"]
    if count > MAXIMUM_UNITS:
        raise ValueError(f"draft inventory exceeds {MAXIMUM_UNITS} units")
    return json.loads(_json(result))


def _payload(profile: dict[str, Any]) -> dict[str, Any]:
    sequences = required_sequences(profile)
    units = []
    for style in profile["supportedStyles"]:
        # Hash the exact style, not a lossy slug; two labels may slug alike.
        style_key = _hash(style)[:16]
        for sequence in sequences:
            coverage = base_key(sequence)
            for layer in profile["pitchLayers"]:
                assignment = [profile["language"], style, coverage, layer]
                identity = _hash(_json(assignment))
                for alternate in range(1, profile["alternateTakes"] + 1):
                    prompt = f"P{len(units) + 1:05d}-{identity}"
                    take = f"{prompt}-t{alternate:02d}"
                    units.append({
                        "promptId": prompt, "takeId": take, "language": profile["language"],
                        "style": style, "kind": sequence["kind"], "phones": list(sequence["phones"]),
                        "pronunciationHint": " ".join(sequence["phones"]), "pitchLayer": layer,
                        "sessionBlock": 1 + len(units) // profile["sessionBlockSize"],
                        "retakeGroup": f"rt-{identity}", "filename": f"takes/s{style_key}/p{layer}/{take}.wav",
                        "coverageKey": coverage, "assignmentId": identity,
                    })
    return {"schemaVersion": 2, "generatorVersion": VERSION, **profile,
            "rangeAssessment": {"status": "NOT_ASSESSED"},
            "requiredCoverage": [base_key(s) for s in sequences], "units": units}


def render_draft_operator_csv(inventory: dict[str, Any]) -> str:
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=CSV_FIELDS, extrasaction="ignore", lineterminator="\n")
    writer.writeheader()
    for unit in inventory["units"]:
        writer.writerow({**unit, "phones": " ".join(unit["phones"])})
    return output.getvalue()


def generate_draft_inventory(profile: dict[str, Any]) -> dict[str, Any]:
    payload = _payload(normalize_draft_profile(profile))
    return {**payload, "inventorySha256": _hash(_json(payload)),
            "scriptSha256": _hash(render_draft_operator_csv(payload))}


def validate_draft_inventory(inventory: Any) -> list[str]:
    if not isinstance(inventory, dict):
        return ["draft inventory must be an object"]
    if not isinstance(inventory.get("units"), list) or len(inventory["units"]) > MAXIMUM_UNITS:
        return ["draft inventory units exceed bounds or are not a list"]
    try:
        expected = generate_draft_inventory({key: inventory[key] for key in PROFILE_FIELDS})
        actual_json = _json(inventory)
    except (KeyError, ValueError, TypeError, OverflowError, UnicodeError, RecursionError) as error:
        return [f"invalid draft profile: {error}"]
    # Exact deterministic reconstruction validates coverage/style/layer/take
    # ownership, paths, assessments and both hashes, including rehashed edits.
    if actual_json == _json(expected):
        return []
    errors = [f"draft inventory field differs from deterministic profile: {key}"
              for key in expected if _json(inventory.get(key)) != _json(expected[key])]
    if set(inventory) - set(expected):
        errors.append("draft inventory contains unknown fields")
    return errors


def draft_production_assignments(inventory: dict[str, Any]) -> list[dict[str, Any]]:
    errors = validate_draft_inventory(inventory)
    if errors:
        raise ValueError("invalid draft inventory: " + "; ".join(errors))
    assignments: dict[str, dict[str, Any]] = {}
    for unit in inventory["units"]:
        assignments.setdefault(unit["assignmentId"], {
            "language": unit["language"], "style": unit["style"],
            "coverageKey": unit["coverageKey"], "pitchLayer": unit["pitchLayer"],
            "promptId": unit["promptId"], "plannedTakeId": unit["takeId"],
            "takeId": "", "state": "MISSING", "markerReviewed": False, "pitchReviewed": False,
        })
    return list(assignments.values())
