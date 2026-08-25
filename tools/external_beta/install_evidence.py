from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
PLATFORMS = {"macos": "arm64", "windows": "x86_64"}
INSTALL_ROW_IDS = tuple(f"INSTALL-{index:03d}" for index in range(1, 13))
FORBIDDEN_PATH_PARTS = {"source", "build", ".git", "build-baseline2", "node_modules"}


@dataclass(frozen=True, slots=True)
class InstallEvidenceResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, Any]:
        return {"passed": self.passed, "errors": list(self.errors), "blocked": list(self.blocked)}


def _hex(value: Any) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def _time(value: Any) -> bool:
    if not isinstance(value, str) or not value:
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def _safe_relative(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in {"", ".", ".."} for part in path.parts)


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_install_matrix(matrix: dict[str, Any]) -> InstallEvidenceResult:
    errors: list[str] = []
    if not isinstance(matrix, dict):
        return InstallEvidenceResult(False, ("install matrix must be an object",))
    if matrix.get("schemaVersion") != 1:
        errors.append("matrix.schemaVersion must be 1")
    if matrix.get("matrixId") != "project-seam.external-beta.install.v1":
        errors.append("matrix.matrixId is invalid")
    targets = matrix.get("targetPlatforms")
    seen_targets: set[tuple[Any, Any]] = set()
    if not isinstance(targets, list):
        errors.append("matrix.targetPlatforms must be an array")
    else:
        for target in targets:
            if not isinstance(target, dict):
                errors.append("matrix target must be an object")
                continue
            key = (target.get("platform"), target.get("architecture"))
            seen_targets.add(key)
            if PLATFORMS.get(target.get("platform")) != target.get("architecture"):
                errors.append(f"matrix target has unsupported platform/architecture: {key}")
    if seen_targets != set(PLATFORMS.items()):
        errors.append("matrix must declare macOS arm64 and Windows x86_64 exactly")
    rows = matrix.get("rows")
    row_ids: list[Any] = []
    if not isinstance(rows, list):
        errors.append("matrix.rows must be an array")
    else:
        for index, row in enumerate(rows):
            label = f"matrix.rows[{index}]"
            if not isinstance(row, dict):
                errors.append(f"{label} must be an object")
                continue
            row_id = row.get("id")
            row_ids.append(row_id)
            if row_id not in INSTALL_ROW_IDS:
                errors.append(f"{label}.id is not canonical")
            for key in ("flow", "title"):
                if not isinstance(row.get(key), str) or not row[key]:
                    errors.append(f"{label}.{key} is required")
    if tuple(row_ids) != INSTALL_ROW_IDS:
        errors.append("matrix rows must contain INSTALL-001 through INSTALL-012 in order")
    return InstallEvidenceResult(not errors, tuple(errors), ())


def _evidence(root: Path, value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return
    for key in ("kind", "path", "sha256", "capturedAt", "reviewer"):
        if not value.get(key):
            errors.append(f"{label}.{key} is required")
    path_value = value.get("path")
    if not _safe_relative(path_value):
        errors.append(f"{label}.path must be a safe relative path")
        return
    parts = {part.lower() for part in PurePosixPath(path_value).parts}
    if parts & FORBIDDEN_PATH_PARTS:
        errors.append(f"{label}.path cannot reference source/build material")
    if not _hex(value.get("sha256")):
        errors.append(f"{label}.sha256 must be a 64-character digest")
    path = root / path_value
    try:
        if path.is_symlink() or not path.is_file():
            errors.append(f"{label}.path must be a regular file")
            return
        resolved = path.resolve(strict=True)
        root_resolved = root.resolve(strict=True)
        if root_resolved != resolved and root_resolved not in resolved.parents:
            errors.append(f"{label}.path escapes evidence root")
            return
        if _hex(value.get("sha256")) and _digest(resolved) != value["sha256"].lower():
            errors.append(f"{label}.sha256 does not match artifact bytes")
    except (FileNotFoundError, OSError) as exc:
        errors.append(f"{label}.path cannot be inspected: {exc}")


def validate_install_record(record: dict[str, Any], matrix: dict[str, Any], root: Path) -> InstallEvidenceResult:
    errors: list[str] = []
    blocked: list[str] = []
    matrix_result = validate_install_matrix(matrix)
    errors.extend(f"matrix: {error}" for error in matrix_result.errors)
    if not isinstance(record, dict):
        return InstallEvidenceResult(False, tuple(errors + ["install record must be an object"]), ())
    if record.get("schemaVersion") != 1:
        errors.append("record.schemaVersion must be 1")
    if record.get("recordType") != "external-beta-install-lifecycle":
        errors.append("record.recordType is invalid")
    platform = record.get("platform")
    if PLATFORMS.get(platform) != record.get("architecture"):
        errors.append("record platform/architecture is outside the target matrix")
    for key in ("recordId", "osBuild", "imageId", "candidateRootId", "operator", "verifier", "startedAt", "endedAt", "clockAuthority"):
        if not record.get(key):
            errors.append(f"record.{key} is required")
    if record.get("accountAuthority") != "clean-verifier-snapshot":
        errors.append("record.accountAuthority must be clean-verifier-snapshot")
    if record.get("clockAuthority") != "physical-device-clock":
        errors.append("record.clockAuthority must be physical-device-clock")
    if not _time(record.get("startedAt")) or not _time(record.get("endedAt")):
        errors.append("record startedAt and endedAt must be ISO-8601 timestamps")
    for key in ("deliverableSha256", "installerSha256", "installedTreeSha256"):
        if not _hex(record.get(key)):
            errors.append(f"record.{key} must be a 64-character digest")
    bank = record.get("bankIdentity")
    if not isinstance(bank, dict) or not isinstance(bank.get("id"), str) or not bank["id"].startswith("beta."):
        errors.append("bankIdentity must name a non-official beta bank")
    elif bank["id"] == "official.voice.01":
        errors.append("install evidence cannot use the Official Voicebank fixture")
    if not isinstance(bank, dict) or not _hex(bank.get("contentSha256")) or not _hex(bank.get("installedProvenanceTreeSha256")):
        errors.append("bankIdentity hashes must be 64-character digests")
    acquisition = record.get("acquisition")
    if not isinstance(acquisition, dict) or not acquisition.get("channel") or not _hex(acquisition.get("envelopeManifestSha256")):
        errors.append("acquisition must identify a governed envelope and manifest hash")
    elif acquisition.get("networkDisabledAfterAcquisition") is not True:
        errors.append("network must be disabled after envelope acquisition")
    inventory = record.get("inventory")
    if not isinstance(inventory, dict):
        errors.append("inventory must be an object")
    else:
        for key in ("preInstallSha256", "postInstallSha256", "postUninstallSha256"):
            if not _hex(inventory.get(key)):
                errors.append(f"inventory.{key} must be a 64-character digest")
        if inventory.get("preservedUserDataCanaries") is not True:
            errors.append("inventory must preserve user-data canaries")
        if inventory.get("residualOwnedCode") is not False:
            errors.append("inventory.residualOwnedCode must be false")
    rows = record.get("rows")
    seen: set[Any] = set()
    if not isinstance(rows, list):
        errors.append("record.rows must be an array")
        rows = []
    for index, row in enumerate(rows):
        label = f"rows[{index}]"
        if not isinstance(row, dict):
            errors.append(f"{label} must be an object")
            continue
        row_id = row.get("id")
        seen.add(row_id)
        if row.get("status") != "PASS":
            errors.append(f"{label}.status must be PASS")
            blocked.append(str(row_id))
        if row_id not in INSTALL_ROW_IDS:
            errors.append(f"{label}.id is not canonical")
        for key in ("preInventorySha256", "postInventorySha256"):
            if not _hex(row.get(key)):
                errors.append(f"{label}.{key} must be a 64-character digest")
        evidence = row.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            errors.append(f"{label}.evidence must be non-empty")
            continue
        for evidence_index, item in enumerate(evidence):
            _evidence(root, item, f"{label}.evidence[{evidence_index}]", errors)
    for row_id in INSTALL_ROW_IDS:
        if row_id not in seen:
            errors.append(f"canonical install row is missing: {row_id}")
            blocked.append(row_id)
    if record.get("status") != "PASS":
        errors.append("record.status must be PASS")
        blocked.append("record")
    return InstallEvidenceResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate External Beta clean-install lifecycle evidence")
    parser.add_argument("--matrix", type=Path, default=Path("docs/product/external-beta-install-matrix.json"))
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = validate_install_record(load_json(args.record), load_json(args.matrix), args.evidence_root)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        payload = {"passed": False, "errors": [str(exc)], "blocked": []}
        print(json.dumps(payload, sort_keys=True))
        return 0 if args.expect_blocked else 2
    text = json.dumps(result.as_dict(), ensure_ascii=False, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
