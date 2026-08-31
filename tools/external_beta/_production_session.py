from __future__ import annotations

from pathlib import Path
from typing import Any

from ._production_common import (
    ALLOWED_SESSION_STATUS,
    ProductionResult,
    check_artifact,
    inventory_index,
    is_hex_digest,
    is_timestamp,
    take_quality_errors,
)


def validate_recording_session(
    session: dict[str, Any], inventory: dict[str, Any], root: Path
) -> ProductionResult:
    errors: list[str] = []
    blocked: list[str] = []
    if not isinstance(session, dict):
        return ProductionResult(False, ("session must be an object",), ())
    if session.get("schemaVersion") != 1:
        errors.append("session.schemaVersion must be 1")
    status = session.get("status")
    if status not in ALLOWED_SESSION_STATUS:
        errors.append("session.status is invalid")
    if status in {"NOT_RUN", "IN_PROGRESS"}:
        blocked.append("session")
    required = (
        "sessionId", "generatorVersion", "inventorySha256", "scriptSha256",
        "performerRef", "startedAt", "endedAt",
    )
    for key in required:
        if not isinstance(session.get(key), str) or not session[key]:
            errors.append(f"session.{key} is required")
    if not is_timestamp(session.get("startedAt")) or not is_timestamp(session.get("endedAt")):
        errors.append("session startedAt and endedAt must be ISO-8601 timestamps")
    for key in ("sampleRate", "bitDepth", "channels"):
        if not isinstance(session.get(key), int) or isinstance(session.get(key), bool):
            errors.append(f"session.{key} must be an integer")
    if session.get("sampleRate") != 48000:
        errors.append("session.sampleRate must be 48000")
    if session.get("bitDepth") != 24:
        errors.append("session.bitDepth must be 24")
    if session.get("channels") != 1:
        errors.append("session.channels must be 1 for dry voicebank source")
    if session.get("rawImmutable") is not True:
        errors.append("session.rawImmutable must be true")
    if not is_hex_digest(session.get("inventorySha256")):
        errors.append("session.inventorySha256 is required")
    expected_takes, coverage, layers = inventory_index(inventory, errors)
    if session.get("inventorySha256") != inventory.get("inventorySha256"):
        errors.append("session.inventorySha256 does not match the generated inventory")
    if session.get("generatorVersion") != inventory.get("generatorVersion"):
        errors.append("session.generatorVersion does not match the generated inventory")
    if session.get("scriptSha256") != inventory.get("scriptSha256"):
        errors.append("session.scriptSha256 does not match the generated operator script")
    takes = session.get("takes")
    if not isinstance(takes, list) or not takes:
        errors.append("session.takes must be a non-empty list")
        takes = []
    seen_take_ids: set[str] = set()
    for index, take in enumerate(takes):
        label = f"session.takes[{index}]"
        if not isinstance(take, dict):
            errors.append(f"{label} must be an object")
            continue
        for key in ("promptId", "takeId", "coverageKey", "pitchLayer", "status", "source", "derived", "quality"):
            if key not in take:
                errors.append(f"{label}.{key} is required")
        take_id = take.get("takeId")
        if not isinstance(take_id, str) or take_id in seen_take_ids:
            errors.append(f"{label}.takeId is missing or duplicated")
            continue
        seen_take_ids.add(take_id)
        expected = expected_takes.get(take_id)
        if expected is None:
            errors.append(f"{label}.takeId is not present in the generated inventory")
        else:
            if take.get("promptId") != expected.get("promptId"):
                errors.append(f"{label}.promptId does not match inventory")
            if take.get("coverageKey") != expected.get("coverageKey"):
                errors.append(f"{label}.coverageKey does not match inventory")
            if take.get("pitchLayer") != expected.get("pitchLayer"):
                errors.append(f"{label}.pitchLayer does not match inventory")
        if take.get("status") not in {"ACCEPTED", "RETAKE", "REJECTED"}:
            errors.append(f"{label}.status is invalid")
        source = take.get("source")
        derived = take.get("derived")
        if not isinstance(source, dict) or not isinstance(derived, dict):
            errors.append(f"{label}.source and derived artifacts are required")
        else:
            check_artifact(root, source, f"{label}.source", errors)
            check_artifact(root, derived, f"{label}.derived", errors)
            if source.get("immutable") is not True:
                errors.append(f"{label}.source.immutable must be true")
        errors.extend(take_quality_errors(take, label))
    if status == "COMPLETE":
        expected_coverage = {(key, layer) for key in coverage for layer in layers}
        actual_coverage = {
            (take.get("coverageKey"), take.get("pitchLayer"))
            for take in takes if isinstance(take, dict) and take.get("status") == "ACCEPTED"
        }
        for key, layer in sorted(expected_coverage - actual_coverage):
            errors.append(f"complete session is missing accepted coverage {key} at pitch layer {layer}")
    return ProductionResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))
