from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
HEX40 = re.compile(r"^[0-9a-fA-F]{40}$")
PLATFORMS = {"macos": "arm64", "windows": "x86_64"}
UA_ROW_IDS = tuple(f"UA-{index:03d}" for index in range(1, 21))


@dataclass(frozen=True, slots=True)
class StandaloneEvidenceResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors), "blocked": list(self.blocked)}


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def _safe_relative(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in {"", ".", ".."} for part in path.parts)


def _hex(value: Any, length: int = 64) -> bool:
    pattern = HEX64 if length == 64 else HEX40
    return isinstance(value, str) and pattern.fullmatch(value) is not None


def _time(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
        return True
    except ValueError:
        return False


def validate_matrix(matrix: dict[str, Any]) -> StandaloneEvidenceResult:
    errors: list[str] = []
    if not isinstance(matrix, dict):
        return StandaloneEvidenceResult(False, ("standalone matrix must be an object",))
    if matrix.get("schemaVersion") != 1:
        errors.append("matrix.schemaVersion must be 1")
    if matrix.get("matrixId") != "project-seam.external-beta.standalone.v1":
        errors.append("matrix.matrixId is invalid")
    targets = matrix.get("targetPlatforms")
    if not isinstance(targets, list):
        errors.append("matrix.targetPlatforms must be an array")
    else:
        seen_targets: set[tuple[Any, Any]] = set()
        for target in targets:
            if not isinstance(target, dict):
                errors.append("matrix target must be an object")
                continue
            key = (target.get("platform"), target.get("architecture"))
            seen_targets.add(key)
            if PLATFORMS.get(target.get("platform")) != target.get("architecture"):
                errors.append(f"matrix target has unsupported platform/architecture: {key}")
        if set(PLATFORMS.items()) != seen_targets:
            errors.append("matrix must declare Apple Silicon macOS and Windows x64 exactly")
    rows = matrix.get("rows")
    if not isinstance(rows, list):
        errors.append("matrix.rows must be an array")
        rows = []
    row_ids: list[Any] = []
    for index, row in enumerate(rows):
        label = f"matrix.rows[{index}]"
        if not isinstance(row, dict):
            errors.append(f"{label} must be an object")
            continue
        row_id = row.get("id")
        row_ids.append(row_id)
        if row_id not in UA_ROW_IDS:
            errors.append(f"{label}.id is not a canonical UA row")
        for key in ("flow", "title", "evidenceKinds"):
            if not row.get(key):
                errors.append(f"{label}.{key} is required")
        if not isinstance(row.get("evidenceKinds"), list) or any(not isinstance(item, str) for item in row.get("evidenceKinds", [])):
            errors.append(f"{label}.evidenceKinds must be a string array")
    if tuple(row_ids) != UA_ROW_IDS:
        errors.append("matrix rows must contain UA-001 through UA-020 in canonical order")
    return StandaloneEvidenceResult(not errors, tuple(errors), ())


def _check_evidence(root: Path, evidence: Any, label: str, errors: list[str]) -> None:
    if not isinstance(evidence, dict):
        errors.append(f"{label} must be an object")
        return
    for key in ("kind", "path", "sha256", "capturedAt", "reviewer"):
        if not evidence.get(key):
            errors.append(f"{label}.{key} is required")
    if not _safe_relative(evidence.get("path")):
        errors.append(f"{label}.path must be a safe relative path")
        return
    if not _hex(evidence.get("sha256")):
        errors.append(f"{label}.sha256 must be a 64-character hexadecimal digest")
    path = root / evidence["path"]
    try:
        root_resolved = root.resolve(strict=True)
        if path.is_symlink():
            errors.append(f"{label}.path must not be a symbolic link")
            return
        resolved = path.resolve(strict=True)
        if root_resolved != resolved and root_resolved not in resolved.parents:
            errors.append(f"{label}.path escapes evidence root")
            return
        if not resolved.is_file():
            errors.append(f"{label}.path is not a regular file")
            return
        digest = hashlib.sha256(resolved.read_bytes()).hexdigest()
        if _hex(evidence.get("sha256")) and digest != evidence["sha256"].lower():
            errors.append(f"{label}.sha256 does not match artifact bytes")
    except FileNotFoundError:
        errors.append(f"{label}.path does not exist")
    except OSError as exc:
        errors.append(f"{label}.path cannot be inspected: {exc}")


def _identity_errors(identity: Any, label: str, errors: list[str]) -> None:
    if not isinstance(identity, dict):
        errors.append(f"{label} must be an object")
        return
    for key in ("version", "buildId", "sourceCommit", "installedTreeSha256"):
        if not identity.get(key):
            errors.append(f"{label}.{key} is required")
    if not _hex(identity.get("sourceCommit"), 40):
        errors.append(f"{label}.sourceCommit must be a 40-character commit")
    if not _hex(identity.get("installedTreeSha256")):
        errors.append(f"{label}.installedTreeSha256 must be a 64-character digest")


def validate_standalone_record(record: dict[str, Any], matrix: dict[str, Any], root: Path) -> StandaloneEvidenceResult:
    errors: list[str] = []
    blocked: list[str] = []
    matrix_result = validate_matrix(matrix)
    errors.extend(f"matrix: {error}" for error in matrix_result.errors)
    if not isinstance(record, dict):
        return StandaloneEvidenceResult(False, tuple(errors + ["standalone record must be an object"]), ())
    if record.get("schemaVersion") != 1:
        errors.append("record.schemaVersion must be 1")
    if record.get("recordType") != "engineering-standalone-journey":
        errors.append("record.recordType is invalid")
    if record.get("engineeringQualification") is not True:
        errors.append("record.engineeringQualification must be true")
    platform = record.get("platform")
    architecture = record.get("architecture")
    if PLATFORMS.get(platform) != architecture:
        errors.append("record platform/architecture is outside the External Beta target matrix")
    for key in ("recordId", "osBuild", "operator", "startedAt", "endedAt", "workloadId", "machineProfileId", "clockAuthority"):
        if not record.get(key):
            errors.append(f"record.{key} is required")
    if not _time(record.get("startedAt")) or not _time(record.get("endedAt")):
        errors.append("record startedAt and endedAt must be ISO-8601 timestamps")
    if record.get("clockAuthority") == "threaded-test-clock" or record.get("clockAuthority") != "physical-device-clock":
        errors.append("record must use a physical-device clock")
    _identity_errors(record.get("appIdentity"), "appIdentity", errors)
    bank = record.get("bankIdentity")
    if not isinstance(bank, dict):
        errors.append("bankIdentity must be an object")
    else:
        for key in ("id", "version", "contentSha256", "installedProvenanceTreeSha256"):
            if not bank.get(key):
                errors.append(f"bankIdentity.{key} is required")
        if not _hex(bank.get("contentSha256")) or not _hex(bank.get("installedProvenanceTreeSha256")):
            errors.append("bankIdentity hashes must be 64-character digests")
        if bank.get("id") == "official.voice.01":
            errors.append("standalone evidence cannot use the Phase 13B Official Voicebank fixture")
    project = record.get("projectIdentity")
    if not isinstance(project, dict):
        errors.append("projectIdentity must be an object")
    else:
        for key in ("projectSha256", "mediaSha256"):
            if not _hex(project.get(key)):
                errors.append(f"projectIdentity.{key} must be a 64-character digest")
    device = record.get("device")
    if not isinstance(device, dict):
        errors.append("device must be an object")
    else:
        for key in ("deviceId", "sampleRate", "blockSize", "channels", "authority"):
            if key not in device:
                errors.append(f"device.{key} is required")
        if device.get("authority") != "physical":
            errors.append("device.authority must be physical")
        if device.get("sampleRate") not in {44100, 48000, 96000}:
            errors.append("device.sampleRate is unsupported")
        if not isinstance(device.get("blockSize"), int) or device.get("blockSize", 0) <= 0:
            errors.append("device.blockSize must be positive")
        if device.get("channels") != 2:
            errors.append("device.channels must be 2 for standalone stereo output")
    for key in ("workloadSha256", "machineProfileSha256"):
        if not _hex(record.get(key)):
            errors.append(f"record.{key} must be a 64-character digest")
    comparison = record.get("comparisonPolicy")
    if not isinstance(comparison, dict) or comparison.get("crossPlatformByteIdentity") is not False or not comparison.get("crossPlatformTolerance"):
        errors.append("comparisonPolicy must declare tolerance-based cross-platform comparison")
    rows = record.get("rows")
    if not isinstance(rows, list):
        errors.append("record.rows must be an array")
        rows = []
    matrix_rows = {row.get("id"): row for row in matrix.get("rows", []) if isinstance(row, dict)}
    seen: set[str] = set()
    for index, row in enumerate(rows):
        label = f"rows[{index}]"
        if not isinstance(row, dict):
            errors.append(f"{label} must be an object")
            continue
        row_id = row.get("id")
        if row_id in seen:
            errors.append(f"{label}.id is duplicated")
        seen.add(row_id)
        expected = matrix_rows.get(row_id)
        if expected is None:
            errors.append(f"{label}.id is not in the canonical standalone matrix")
        if row.get("status") != "PASS":
            errors.append(f"{label}.status must be PASS")
            blocked.append(str(row_id))
        evidence = row.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            errors.append(f"{label}.evidence must be non-empty")
            continue
        for evidence_index, item in enumerate(evidence):
            _check_evidence(root, item, f"{label}.evidence[{evidence_index}]", errors)
    missing = [row_id for row_id in UA_ROW_IDS if row_id not in seen]
    errors.extend(f"canonical standalone row is missing: {row_id}" for row_id in missing)
    blocked.extend(missing)
    if record.get("status") != "PASS":
        errors.append("record.status must be PASS to promote engineering qualification")
        blocked.append("record")
    return StandaloneEvidenceResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value
