"""Transactional corrections with stale-value checks; edits never grant review."""
import copy
from .labels import label_report


def apply_label_edits(label: dict, edits: list[dict], *, vocabulary: set[str], minimum_confidence: float) -> dict:
    label_report(label, vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    if not isinstance(edits, list) or not 1 <= len(edits) <= 65536:
        raise ValueError("Label corrections require 1..65536 edits")
    result = copy.deepcopy(label)
    seen = set()
    for edit in edits:
        if not isinstance(edit, dict) or set(edit) != {"kind", "index", "expected", "replacement"}:
            raise ValueError("Invalid correction fields")
        kind, index = edit["kind"], edit["index"]
        if kind not in ("pitch", "phoneme") or type(index) is not int or index < 0:
            raise ValueError("Invalid correction target")
        if (kind, index) in seen:
            raise ValueError("Duplicate correction target")
        seen.add((kind, index))
        if kind == "pitch":
            if index >= len(label["f0Hz"]):
                raise ValueError("Pitch correction outside label")
            current = dict(f0Hz=label["f0Hz"][index], voiced=label["voiced"][index])
            replacement = edit["replacement"]
            if not isinstance(replacement, dict) or set(replacement) != {"f0Hz", "voiced"}:
                raise ValueError("Pitch correction requires F0 and voicing together")
            result["f0Hz"][index], result["voiced"][index] = replacement["f0Hz"], replacement["voiced"]
        else:
            if index >= len(label["phonemes"]):
                raise ValueError("Phoneme correction outside label")
            current = label["phonemes"][index]
            result["phonemes"][index] = copy.deepcopy(edit["replacement"])
        if edit["expected"] != current:
            raise ValueError("Correction was made against a stale label value")
    result["reviewRevision"] = None
    report = label_report(result, vocabulary=vocabulary, minimum_confidence=minimum_confidence)
    if any(item["code"] == "voicing-f0-mismatch" for item in report["correctionQueue"]):
        raise ValueError("Corrected label has inconsistent F0 and voicing")
    return result
