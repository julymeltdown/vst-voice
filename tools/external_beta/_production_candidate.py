from __future__ import annotations

from typing import Any, Iterable

from ._production_common import ProductionResult, accepted_takes, is_timestamp


def validate_retake_closure(
    closure: dict[str, Any], inventory: dict[str, Any], sessions: Iterable[dict[str, Any]]
) -> ProductionResult:
    errors: list[str] = []
    blocked: list[str] = []
    if not isinstance(closure, dict):
        return ProductionResult(False, ("retake closure must be an object",), ())
    if closure.get("schemaVersion") != 1:
        errors.append("retake closure schemaVersion must be 1")
    if closure.get("inventorySha256") != inventory.get("inventorySha256"):
        errors.append("retake closure inventorySha256 does not match inventory")
    if closure.get("status") in {"NOT_RUN", "IN_PROGRESS"}:
        blocked.append("retake-closure")
    if closure.get("status") != "PASS":
        errors.append("retake closure status must be PASS")
    open_retakes = closure.get("openRetakes")
    closed_retakes = closure.get("closedRetakes")
    if not isinstance(open_retakes, list) or not isinstance(closed_retakes, list):
        errors.append("openRetakes and closedRetakes must be arrays")
        return ProductionResult(False, tuple(errors), tuple(blocked))
    if open_retakes:
        errors.append("retake closure cannot contain open retakes")
        blocked.append("open-retakes")
    accepted = accepted_takes(sessions)
    seen_prompts: set[str] = set()
    for index, item in enumerate(closed_retakes):
        label = f"closedRetakes[{index}]"
        if not isinstance(item, dict):
            errors.append(f"{label} must be an object")
            continue
        required = (
            "retakeId", "promptId", "originalTakeId", "replacementTakeId",
            "reason", "closedAt", "reviewer", "status",
        )
        for key in required:
            if not item.get(key):
                errors.append(f"{label}.{key} is required")
        if item.get("status") != "CLOSED":
            errors.append(f"{label}.status must be CLOSED")
        if not is_timestamp(item.get("closedAt")):
            errors.append(f"{label}.closedAt must be an ISO-8601 timestamp")
        prompt_id = item.get("promptId")
        if prompt_id in seen_prompts:
            errors.append(f"{label}.promptId has duplicate active closure")
        seen_prompts.add(prompt_id)
        if item.get("originalTakeId") == item.get("replacementTakeId"):
            errors.append(f"{label} replacement must differ from original take")
        replacement = accepted.get(item.get("replacementTakeId"))
        if replacement is None:
            errors.append(f"{label}.replacementTakeId is not an accepted take")
        elif replacement.get("promptId") != prompt_id:
            errors.append(f"{label}.replacementTakeId belongs to another prompt")
    return ProductionResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def validate_candidate_export(
    candidate: dict[str, Any],
    inventory: dict[str, Any],
    sessions: Iterable[dict[str, Any]],
    closure: dict[str, Any],
) -> ProductionResult:
    errors: list[str] = []
    blocked: list[str] = []
    if not isinstance(candidate, dict):
        return ProductionResult(False, ("candidate must be an object",), ())
    if candidate.get("schemaVersion") != 1:
        errors.append("candidate schemaVersion must be 1")
    if candidate.get("status") != "READY":
        errors.append("candidate export status must be READY")
        blocked.append("candidate-export")
    if candidate.get("inventorySha256") != inventory.get("inventorySha256"):
        errors.append("candidate inventorySha256 does not match inventory")
    closure_result = validate_retake_closure(closure, inventory, sessions)
    errors.extend(closure_result.errors)
    blocked.extend(closure_result.blocked)
    accepted = accepted_takes(sessions)
    coverage = {item for item in inventory.get("requiredCoverage", []) if isinstance(item, str)}
    layers = {item for item in inventory.get("pitchLayers", []) if isinstance(item, int)}
    bindings = candidate.get("unitBindings")
    if not isinstance(bindings, list) or not bindings:
        errors.append("candidate.unitBindings must be a non-empty list")
        bindings = []
    seen_aliases: set[str] = set()
    seen_pairs: set[tuple[str, int]] = set()
    for index, binding in enumerate(bindings):
        label = f"unitBindings[{index}]"
        if not isinstance(binding, dict):
            errors.append(f"{label} must be an object")
            continue
        for key in ("coverageKey", "pitchLayer", "takeId", "alias", "markers", "pitchMarks", "validator"):
            if key not in binding:
                errors.append(f"{label}.{key} is required")
        pair = (binding.get("coverageKey"), binding.get("pitchLayer"))
        if pair in seen_pairs:
            errors.append(f"{label} duplicates coverage and pitch layer")
        seen_pairs.add(pair)
        if binding.get("coverageKey") not in coverage:
            errors.append(f"{label}.coverageKey is not declared")
        if binding.get("pitchLayer") not in layers:
            errors.append(f"{label}.pitchLayer is not declared")
        alias = binding.get("alias")
        if not isinstance(alias, str) or not alias:
            errors.append(f"{label}.alias is required")
        elif alias in seen_aliases:
            errors.append(f"{label}.alias is duplicated")
        else:
            seen_aliases.add(alias)
        take = accepted.get(binding.get("takeId"))
        if take is None:
            errors.append(f"{label}.takeId is not an accepted take")
        elif take.get("coverageKey") != binding.get("coverageKey") or take.get("pitchLayer") != binding.get("pitchLayer"):
            errors.append(f"{label}.takeId does not match coverage and pitch layer")
        markers = binding.get("markers")
        if not isinstance(markers, dict):
            errors.append(f"{label}.markers is required")
        else:
            values = [markers.get(key) for key in ("start", "loopStart", "loopEnd", "releaseEnd")]
            if any(not isinstance(value, (int, float)) or isinstance(value, bool) for value in values):
                errors.append(f"{label}.markers must contain numeric boundaries")
            elif not (values[0] <= values[1] < values[2] <= values[3]):
                errors.append(f"{label}.markers are out of order")
        marks = binding.get("pitchMarks")
        if not isinstance(marks, list) or not marks or any(
            not isinstance(value, (int, float)) or isinstance(value, bool) for value in marks
        ):
            errors.append(f"{label}.pitchMarks must be a non-empty numeric list")
        elif any(left > right for left, right in zip(marks, marks[1:])):
            errors.append(f"{label}.pitchMarks must be sorted")
        validator = binding.get("validator")
        if not isinstance(validator, dict) or validator.get("result") != "PASS" or validator.get("pitchOctaveError") is not False:
            errors.append(f"{label}.validator must explicitly pass with no octave error")
    expected = {(key, layer) for key in coverage for layer in layers}
    missing = sorted(expected - seen_pairs, key=lambda item: (item[0], item[1]))
    errors.extend(f"candidate coverage is missing {key} at pitch layer {layer}" for key, layer in missing)
    if candidate.get("rawRecordingsMutated") is True:
        errors.append("candidate export reports mutated raw recordings")
    return ProductionResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))
