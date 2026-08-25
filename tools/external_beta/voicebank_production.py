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


def _safe_relative(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in {"", ".", ".."} for part in path.parts)


def _hex(value: Any) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def _timestamp(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
        return True
    except ValueError:
        return False


def _inventory_index(inventory: dict[str, Any], errors: list[str]) -> tuple[dict[str, dict[str, Any]], set[str], set[int]]:
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
    return by_take, {item for item in coverage if isinstance(item, str)}, {item for item in layers if isinstance(item, int)}


def _check_artifact(root: Path, item: dict[str, Any], label: str, errors: list[str]) -> None:
    path_text = item.get("path")
    if not _safe_relative(path_text):
        errors.append(f"{label}.path must be a safe relative path")
        return
    if not _hex(item.get("sha256")):
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
        if _hex(item.get("sha256")) and sha256_file(resolved) != item["sha256"].lower():
            errors.append(f"{label}.sha256 does not match file bytes")
    except FileNotFoundError:
        errors.append(f"{label}.path does not exist")
    except OSError as exc:
        errors.append(f"{label}.path cannot be inspected: {exc}")


def _take_quality_errors(take: dict[str, Any], label: str) -> list[str]:
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
    if isinstance(quality.get("rootMidi"), int) and isinstance(quality.get("analyzedMidi"), int) and abs(quality["rootMidi"] - quality["analyzedMidi"]) > 1:
        errors.append(f"{label}.quality root-pitch octave error")
    return errors


def validate_recording_session(session: dict[str, Any], inventory: dict[str, Any], root: Path) -> ProductionResult:
    errors: list[str] = []
    blocked: list[str] = []
    if not isinstance(session, dict):
        return ProductionResult(False, ("session must be an object",), (),)
    if session.get("schemaVersion") != 1:
        errors.append("session.schemaVersion must be 1")
    status = session.get("status")
    if status not in ALLOWED_SESSION_STATUS:
        errors.append("session.status is invalid")
    if status in {"NOT_RUN", "IN_PROGRESS"}:
        blocked.append("session")
    for key in ("sessionId", "generatorVersion", "inventorySha256", "scriptSha256", "performerRef", "startedAt", "endedAt"):
        if not isinstance(session.get(key), str) or not session[key]:
            errors.append(f"session.{key} is required")
    if not _timestamp(session.get("startedAt")) or not _timestamp(session.get("endedAt")):
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
    if not isinstance(session.get("inventorySha256"), str) or not _hex(session.get("inventorySha256")):
        errors.append("session.inventorySha256 is required")
    expected_takes, coverage, layers = _inventory_index(inventory, errors)
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
            _check_artifact(root, source, f"{label}.source", errors)
            _check_artifact(root, derived, f"{label}.derived", errors)
            if source.get("immutable") is not True:
                errors.append(f"{label}.source.immutable must be true")
        errors.extend(_take_quality_errors(take, label))
    if status == "COMPLETE":
        expected_coverage = {(key, layer) for key in coverage for layer in layers}
        actual_coverage = {(take.get("coverageKey"), take.get("pitchLayer")) for take in takes if isinstance(take, dict) and take.get("status") == "ACCEPTED"}
        for key, layer in sorted(expected_coverage - actual_coverage):
            errors.append(f"complete session is missing accepted coverage {key} at pitch layer {layer}")
    return ProductionResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def _accepted_takes(sessions: Iterable[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    accepted: dict[str, dict[str, Any]] = {}
    for session in sessions:
        for take in session.get("takes", []) if isinstance(session, dict) and isinstance(session.get("takes"), list) else []:
            if isinstance(take, dict) and take.get("status") == "ACCEPTED" and isinstance(take.get("takeId"), str):
                accepted[take["takeId"]] = take
    return accepted


def validate_retake_closure(closure: dict[str, Any], inventory: dict[str, Any], sessions: Iterable[dict[str, Any]]) -> ProductionResult:
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
    accepted = _accepted_takes(sessions)
    seen_prompts: set[str] = set()
    for index, item in enumerate(closed_retakes):
        label = f"closedRetakes[{index}]"
        if not isinstance(item, dict):
            errors.append(f"{label} must be an object")
            continue
        for key in ("retakeId", "promptId", "originalTakeId", "replacementTakeId", "reason", "closedAt", "reviewer", "status"):
            if not item.get(key):
                errors.append(f"{label}.{key} is required")
        if item.get("status") != "CLOSED":
            errors.append(f"{label}.status must be CLOSED")
        if not _timestamp(item.get("closedAt")):
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


def validate_candidate_export(candidate: dict[str, Any], inventory: dict[str, Any], sessions: Iterable[dict[str, Any]], closure: dict[str, Any]) -> ProductionResult:
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
    accepted = _accepted_takes(sessions)
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
        else:
            if take.get("coverageKey") != binding.get("coverageKey") or take.get("pitchLayer") != binding.get("pitchLayer"):
                errors.append(f"{label}.takeId does not match coverage and pitch layer")
        markers = binding.get("markers")
        if not isinstance(markers, dict):
            errors.append(f"{label}.markers is required")
        else:
            marker_values = [markers.get(key) for key in ("start", "loopStart", "loopEnd", "releaseEnd")]
            if any(not isinstance(value, (int, float)) or isinstance(value, bool) for value in marker_values):
                errors.append(f"{label}.markers must contain numeric boundaries")
            elif not (marker_values[0] <= marker_values[1] < marker_values[2] <= marker_values[3]):
                errors.append(f"{label}.markers are out of order")
        marks = binding.get("pitchMarks")
        if not isinstance(marks, list) or not marks or any(not isinstance(value, (int, float)) or isinstance(value, bool) for value in marks):
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


def create_beta_lock(candidate: dict[str, Any], package: dict[str, Any], canonical_song: dict[str, Any], inventory: dict[str, Any], *, generated_at: str) -> dict[str, Any]:
    candidate_digest = sha256_json(candidate)
    package_digest = package.get("contentSha256")
    return {
        "schemaVersion": 1,
        "status": "LOCKED",
        "voicebankId": package.get("id"),
        "version": package.get("version"),
        "packageSha256": package_digest,
        "entryManifestSha256": package.get("entryManifestSha256"),
        "candidateSha256": candidate_digest,
        "inventorySha256": inventory.get("inventorySha256"),
        "sourceDerivedTreeSha256": sha256_json({"sourceAssets": candidate.get("sourceAssets", []), "derivedAssets": candidate.get("derivedAssets", [])}),
        "canonicalSong": {
            "projectSha256": canonical_song.get("projectSha256"),
            "mediaSha256": canonical_song.get("mediaSha256"),
        },
        "generatedAt": generated_at,
        "contentChangePolicy": "any package, inventory, candidate, source, derived, project, or media change creates a new lock",
    }


def validate_beta_lock(lock: dict[str, Any], candidate: dict[str, Any], package: dict[str, Any], inventory: dict[str, Any], canonical_song: dict[str, Any]) -> ProductionResult:
    errors: list[str] = []
    for key in ("schemaVersion", "status", "voicebankId", "version", "packageSha256", "entryManifestSha256", "candidateSha256", "inventorySha256", "sourceDerivedTreeSha256", "canonicalSong", "generatedAt"):
        if key not in lock:
            errors.append(f"lock.{key} is required")
    if lock.get("schemaVersion") != 1 or lock.get("status") != "LOCKED":
        errors.append("lock must be schemaVersion 1 and LOCKED")
    if lock.get("voicebankId") != package.get("id") or lock.get("version") != package.get("version"):
        errors.append("lock bank identity does not match package")
    if lock.get("packageSha256") != package.get("contentSha256"):
        errors.append("lock packageSha256 does not match package")
    if lock.get("entryManifestSha256") != package.get("entryManifestSha256"):
        errors.append("lock entryManifestSha256 does not match package")
    if lock.get("candidateSha256") != sha256_json(candidate):
        errors.append("lock candidateSha256 does not match candidate bytes")
    if lock.get("inventorySha256") != inventory.get("inventorySha256"):
        errors.append("lock inventorySha256 does not match inventory")
    song = lock.get("canonicalSong")
    if not isinstance(song, dict) or song.get("projectSha256") != canonical_song.get("projectSha256") or song.get("mediaSha256") != canonical_song.get("mediaSha256"):
        errors.append("lock canonicalSong hashes do not match canonical song")
    if not _timestamp(lock.get("generatedAt")):
        errors.append("lock.generatedAt must be an ISO-8601 timestamp")
    return ProductionResult(not errors, tuple(errors), ())
