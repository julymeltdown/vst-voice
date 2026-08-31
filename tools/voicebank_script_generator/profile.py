from __future__ import annotations

import json
import re
from typing import Any


GENERATOR_VERSION = "seam-beta-inventory-1.0.0"
SAFE_NAME = re.compile(r"^[A-Za-z0-9._/-]+$")
CSV_FIELDS = (
    "promptId", "takeId", "kind", "phones", "pronunciationHint",
    "pitchLayer", "sessionBlock", "retakeGroup", "filename",
)

DEFAULT_PROFILE: dict[str, Any] = {
    "profileId": "beta-ja-cvvc-v1",
    "language": "ja",
    "supportedStyles": ["original"],
    "vowels": ["a", "i", "u", "e", "o"],
    "consonants": [
        "k", "g", "s", "sh", "z", "j", "t", "ch", "ts", "d", "n", "h", "f",
        "b", "p", "m", "y", "r", "w", "v", "ky", "gy", "ny", "hy", "by", "py",
        "my", "ry", "fy", "vy",
    ],
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


def slug(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9]+", "-", value).strip("-").lower()
    return result or "unit"


def safe_filename(value: Any) -> bool:
    if not isinstance(value, str) or not SAFE_NAME.fullmatch(value):
        return False
    return all(part not in {"", ".", ".."} for part in value.split("/"))


def _as_string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not value or any(not isinstance(item, str) or not item for item in value):
        raise ValueError(f"{label} must be a non-empty list of strings")
    if len(set(value)) != len(value):
        raise ValueError(f"{label} contains duplicate aliases")
    return list(value)


def normalize_profile(profile: dict[str, Any] | None = None) -> dict[str, Any]:
    merged = json.loads(json.dumps(DEFAULT_PROFILE, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    if profile:
        merged.update(profile)
    if not isinstance(merged.get("profileId"), str) or not merged["profileId"]:
        raise ValueError("profileId is required")
    if merged.get("language") != "ja":
        raise ValueError("language must be ja")
    for key in ("supportedStyles", "vowels", "consonants", "specialPhones"):
        merged[key] = _as_string_list(merged.get(key), key)
    allowed_kinds = {"sustain", "release", "breath", "glottal-attack", "special", "cv", "vc", "vv"}
    merged["includeKinds"] = _as_string_list(merged.get("includeKinds"), "includeKinds")
    unknown = set(merged["includeKinds"]) - allowed_kinds
    if unknown:
        raise ValueError(f"includeKinds contains unsupported kinds: {sorted(unknown)}")
    layers = merged.get("pitchLayers")
    if (
        not isinstance(layers, list)
        or len(layers) not in {2, 3}
        or any(isinstance(item, bool) or not isinstance(item, int) for item in layers)
    ):
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
    if (
        not isinstance(range_test.get("minMidi"), int)
        or not isinstance(range_test.get("maxMidi"), int)
        or range_test["minMidi"] >= range_test["maxMidi"]
    ):
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
        sequences.extend({"kind": "sustain", "phones": [vowel]} for vowel in vowels)
    if "release" in kinds:
        sequences.extend({"kind": "release", "phones": [vowel, "R"]} for vowel in vowels)
    if "breath" in kinds:
        sequences.append({"kind": "breath", "phones": ["br"]})
    if "glottal-attack" in kinds:
        sequences.extend({"kind": "glottal-attack", "phones": ["glottal", vowel]} for vowel in vowels)
    if "special" in kinds:
        sequences.extend({"kind": "special", "phones": [phone]} for phone in special)
    if "cv" in kinds:
        sequences.extend({"kind": "cv", "phones": [consonant, vowel]} for consonant in consonants for vowel in vowels)
    if "vc" in kinds:
        sequences.extend({"kind": "vc", "phones": [vowel, consonant]} for vowel in vowels for consonant in consonants)
    if "vv" in kinds:
        sequences.extend(
            {"kind": "vv", "phones": [left, right]}
            for left in vowels for right in vowels if left != right
        )
    if not sequences:
        raise ValueError("includeKinds must produce at least one required sequence")
    return sequences


def base_key(sequence: dict[str, Any]) -> str:
    return f"{sequence['kind']}:{':'.join(sequence['phones'])}"


def retake_group(profile: dict[str, Any], coverage_key: str) -> str:
    override = profile.get("retakeGroups", {}).get(coverage_key)
    value = override if override is not None else f"rt-{slug(coverage_key)}"
    if not isinstance(value, str) or not value or not SAFE_NAME.fullmatch(value):
        raise ValueError(f"retake group is unsafe or empty for {coverage_key}")
    return value
