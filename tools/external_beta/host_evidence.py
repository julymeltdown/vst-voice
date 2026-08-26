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
HOST_CHECK_NAMES = (
    "scan", "installDiscovery", "instantiate", "guiLifecycle", "editorResize", "editorReopen",
    "stateSave", "stateRestore", "transport", "tempoAutomation", "liveInput", "expression",
    "offlineExport", "unloadReload", "updateRescan", "uninstallRescan", "channelMatrix",
    "sampleRateMatrix", "bufferMatrix", "projectReopen", "bankRecovery", "activeSession30m",
    "bounceInspection",
)
HOST_IDS = tuple(f"HOST-{index:03d}" for index in range(1, 10))
FORBIDDEN_PATH_PARTS = {"source", "build", ".git", "build-baseline2", "node_modules"}


@dataclass(frozen=True, slots=True)
class HostEvidenceResult:
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


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _installed_digest(path: Path) -> str:
    if path.is_symlink():
        raise ValueError("installed artifact is symbolic")
    if path.is_file():
        return _digest(path)
    if not path.is_dir():
        raise ValueError("installed artifact is not a regular file or directory")
    digest = hashlib.sha256()
    for child in sorted(path.rglob("*")):
        if child.is_symlink():
            raise ValueError("installed artifact tree contains a symbolic link")
        if child.is_file():
            digest.update(child.relative_to(path).as_posix().encode("utf-8"))
            digest.update(b"\0")
            digest.update(bytes.fromhex(_digest(child)))
    return digest.hexdigest()


def _safe_relative(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in {"", ".", ".."} for part in path.parts)


def validate_host_matrix(matrix: dict[str, Any]) -> HostEvidenceResult:
    errors: list[str] = []
    if not isinstance(matrix, dict):
        return HostEvidenceResult(False, ("host matrix must be an object",))
    if matrix.get("schemaVersion") != 1:
        errors.append("matrix.schemaVersion must be 1")
    if matrix.get("matrixId") != "project-seam.external-beta.host.v1":
        errors.append("matrix.matrixId is invalid")
    targets = matrix.get("targetPlatforms")
    seen: set[tuple[Any, Any]] = set()
    if not isinstance(targets, list):
        errors.append("matrix.targetPlatforms must be an array")
    else:
        for target in targets:
            if not isinstance(target, dict):
                errors.append("matrix target must be an object")
                continue
            key = (target.get("platform"), target.get("architecture"))
            seen.add(key)
            if PLATFORMS.get(target.get("platform")) != target.get("architecture"):
                errors.append(f"unsupported matrix platform/architecture: {key}")
    if seen != set(PLATFORMS.items()):
        errors.append("matrix must declare macOS arm64 and Windows x86_64")
    rows = matrix.get("targets")
    ids: list[Any] = []
    if not isinstance(rows, list):
        errors.append("matrix.targets must be an array")
    else:
        for index, row in enumerate(rows):
            label = f"matrix.targets[{index}]"
            if not isinstance(row, dict):
                errors.append(f"{label} must be an object")
                continue
            target_id = row.get("id")
            ids.append(target_id)
            if target_id not in HOST_IDS:
                errors.append(f"{label}.id is not canonical")
            if PLATFORMS.get(row.get("platform")) != row.get("architecture"):
                errors.append(f"{label} platform/architecture is invalid")
            if row.get("host") not in {"reaper", "bitwig", "logic-pro"}:
                errors.append(f"{label}.host is invalid")
            if row.get("format") not in {"CLAP", "VST3", "AUv2"}:
                errors.append(f"{label}.format is invalid")
    if tuple(ids) != HOST_IDS:
        errors.append("matrix targets must contain HOST-001 through HOST-009 in order")
    expected_tuples = {
        ("macos", "reaper", "CLAP"), ("macos", "reaper", "VST3"),
        ("windows", "reaper", "CLAP"), ("windows", "reaper", "VST3"),
        ("macos", "bitwig", "CLAP"), ("macos", "bitwig", "VST3"),
        ("windows", "bitwig", "CLAP"), ("windows", "bitwig", "VST3"),
        ("macos", "logic-pro", "AUv2"),
    }
    actual_tuples = {(row.get("platform"), row.get("host"), row.get("format")) for row in rows if isinstance(row, dict)} if isinstance(rows, list) else set()
    if actual_tuples != expected_tuples:
        errors.append("matrix does not contain the exact required REAPER, Bitwig, and Logic tuples")
    return HostEvidenceResult(not errors, tuple(errors), ())


def _evidence(root: Path, value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return
    for key in ("kind", "path", "sha256", "capturedAt", "reviewer"):
        if not value.get(key):
            errors.append(f"{label}.{key} is required")
    path_value = value.get("path")
    if not _safe_relative(path_value):
        errors.append(f"{label}.path must be safe and relative")
        return
    if {part.lower() for part in PurePosixPath(path_value).parts} & FORBIDDEN_PATH_PARTS:
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


def validate_host_record(record: dict[str, Any], matrix: dict[str, Any], root: Path) -> HostEvidenceResult:
    errors: list[str] = []
    blocked: list[str] = []
    matrix_result = validate_host_matrix(matrix)
    errors.extend(f"matrix: {error}" for error in matrix_result.errors)
    if not isinstance(record, dict):
        return HostEvidenceResult(False, tuple(errors + ["host record must be an object"]), ())
    if record.get("schemaVersion") != 1:
        errors.append("record.schemaVersion must be 1")
    if record.get("recordType") != "external-beta-host-session":
        errors.append("record.recordType is invalid")
    target_id = record.get("targetId")
    targets = {row.get("id"): row for row in matrix.get("targets", []) if isinstance(row, dict)}
    target = targets.get(target_id)
    if target is None:
        errors.append("record targetId is not in the host matrix")
    else:
        tuple_value = (record.get("platform"), record.get("host"), record.get("pluginFormat"))
        expected_tuple = (target.get("platform"), target.get("host"), target.get("format"))
        if tuple_value != expected_tuple or record.get("architecture") != target.get("architecture"):
            errors.append("record target tuple does not match the matrix target")
    if record.get("platform") not in PLATFORMS or PLATFORMS.get(record.get("platform")) != record.get("architecture"):
        errors.append("record platform/architecture is outside the target matrix")
    for key in ("recordId", "hostVersion", "hostBuild", "osBuild", "candidateRootId", "artifactPath", "operator", "verifier", "startedAt", "endedAt", "clockAuthority"):
        if not record.get(key):
            errors.append(f"record.{key} is required")
    if record.get("clockAuthority") != "physical-device-clock":
        errors.append("record.clockAuthority must be physical-device-clock")
    if not _time(record.get("startedAt")) or not _time(record.get("endedAt")):
        errors.append("record startedAt and endedAt must be ISO-8601 timestamps")
    if "build" in str(record.get("artifactPath", "")).lower() or "source" in str(record.get("artifactPath", "")).lower():
        errors.append("artifactPath must identify an installed artifact, not source/build material")
    artifact_value = record.get("artifactPath")
    if isinstance(artifact_value, str) and artifact_value:
        artifact = Path(artifact_value)
        try:
            actual_artifact_digest = _installed_digest(artifact)
            for key in ("pluginSha256", "installedTreeSha256"):
                if record.get(key) != actual_artifact_digest:
                    errors.append(f"record.{key} does not match installed artifact bytes")
        except (OSError, ValueError) as exc:
            errors.append(f"installed artifact cannot be hashed: {exc}")
    for key in ("pluginSha256", "installedTreeSha256", "workloadSha256", "machineProfileSha256"):
        if not _hex(record.get(key)):
            errors.append(f"record.{key} must be a 64-character digest")
    bank = record.get("bankIdentity")
    if not isinstance(bank, dict) or not isinstance(bank.get("id"), str) or not bank["id"].startswith("beta."):
        errors.append("bankIdentity must name a non-official beta bank")
    elif bank["id"] == "official.voice.01":
        errors.append("host evidence cannot use the Official Voicebank fixture")
    if not isinstance(bank, dict) or not _hex(bank.get("contentSha256")) or not _hex(bank.get("installedProvenanceTreeSha256")):
        errors.append("bankIdentity hashes must be 64-character digests")
    project = record.get("projectIdentity")
    if not isinstance(project, dict) or not _hex(project.get("projectSha256")) or not _hex(project.get("mediaSha256")):
        errors.append("projectIdentity hashes must be 64-character digests")
    checks = record.get("checks")
    if not isinstance(checks, dict):
        errors.append("checks must be an object")
        checks = {}
    missing_checks = sorted(set(HOST_CHECK_NAMES) - set(checks))
    if missing_checks:
        errors.append(f"PASS record missing checks: {', '.join(missing_checks)}")
    for name, value in checks.items():
        if name not in HOST_CHECK_NAMES:
            errors.append(f"unknown host check: {name}")
        if value != "PASS":
            errors.append(f"host check {name} must be PASS")
    evidence = record.get("evidence")
    if not isinstance(evidence, list) or not evidence:
        errors.append("PASS record requires evidence")
    else:
        for index, item in enumerate(evidence):
            _evidence(root, item, f"evidence[{index}]", errors)
    if record.get("status") != "PASS":
        errors.append("record.status must be PASS")
        blocked.append("record")
    return HostEvidenceResult(not errors and not blocked, tuple(errors), tuple(sorted(set(blocked))))


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate External Beta host-session evidence")
    parser.add_argument("--matrix", type=Path, default=Path("docs/product/external-beta-host-matrix.json"))
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = validate_host_record(load_json(args.record), load_json(args.matrix), args.evidence_root)
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
