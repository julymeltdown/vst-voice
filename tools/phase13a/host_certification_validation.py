from __future__ import annotations

import hashlib
import json
from pathlib import Path

try:
    from .distribution_manifest import tree_sha256
except ImportError:
    from distribution_manifest import tree_sha256

JsonValue = (
    None
    | bool
    | int
    | float
    | str
    | list["JsonValue"]
    | dict[str, "JsonValue"]
)
JsonObject = dict[str, JsonValue]

RUNTIME_RESULTS = {"NOT_RUN", "BLOCKED", "FAIL", "PASS"}
CHECK_NAMES = {
    "scan",
    "installDiscovery",
    "instantiate",
    "guiLifecycle",
    "editorResize",
    "editorReopen",
    "stateSave",
    "stateRestore",
    "transport",
    "tempoAutomation",
    "liveInput",
    "expression",
    "offlineExport",
    "unloadReload",
    "updateRescan",
    "uninstallRescan",
    "channelMatrix",
    "sampleRateMatrix",
    "bufferMatrix",
    "projectReopen",
}
REQUIRED_PASS_FIELDS = {
    "targetId",
    "runtimeResult",
    "candidateBuildId",
    "osVersion",
    "hostVersion",
    "pluginFormat",
    "artifactPath",
    "toolIdentity",
    "executedAt",
    "executor",
    "checks",
    "evidence",
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _json_sha256(value: JsonObject) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _is_hex(value: JsonValue) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdefABCDEF" for character in value)
    )


def _installed_artifact(
    record: JsonObject, installed_root: Path
) -> tuple[list[str], str | None]:
    errors: list[str] = []
    artifact_value = record.get("artifactPath")
    if not isinstance(artifact_value, str) or not artifact_value.strip():
        return ["artifactPath must identify an installed package"], None
    artifact = Path(artifact_value).expanduser()
    root = Path(installed_root).expanduser()
    if not artifact.is_absolute() or not root.is_absolute():
        return ["artifactPath and installed root must be absolute"], None
    if root.is_symlink():
        errors.append(f"installed root is a symbolic link: {root}")
    if artifact.is_symlink():
        errors.append(f"installed artifact is a symbolic link: {artifact}")
    if errors:
        return errors, None
    if not root.is_dir():
        errors.append(f"installed root does not exist: {root}")
        return errors, None
    if not artifact.exists():
        errors.append(f"installed artifact does not exist: {artifact}")
        return errors, None
    resolved_root = root.resolve()
    resolved_artifact = artifact.resolve()
    if resolved_artifact.parent != resolved_root:
        errors.append("installed artifact must be a direct child of installed root")
        return errors, None
    suffix = {"CLAP": ".clap", "VST3": ".vst3", "AUv2": ".component"}.get(
        record.get("pluginFormat")
    )
    if suffix is None or resolved_artifact.suffix.lower() != suffix.lower():
        errors.append("artifactPath does not match pluginFormat")
        return errors, None
    try:
        actual = tree_sha256(resolved_artifact)
    except (OSError, ValueError) as exc:
        errors.append(f"installed artifact cannot be hashed without links: {exc}")
        return errors, None
    for name in ("pluginSha256", "artifactTreeSha256"):
        declared = record.get(name)
        if declared is not None and declared != actual:
            errors.append(f"caller-supplied {name} does not match installed bytes")
    return errors, actual


def _candidate_errors(
    record: JsonObject, candidate: JsonObject, actual_sha256: str
) -> list[str]:
    errors: list[str] = []
    identity = candidate.get("releaseIdentity")
    if not isinstance(identity, dict) or not isinstance(identity.get("buildId"), str):
        return ["candidate manifest releaseIdentity.buildId is required"]
    if record.get("candidateBuildId") != identity["buildId"]:
        errors.append("record candidateBuildId does not match candidate manifest")
    artifacts = candidate.get("artifacts")
    if not isinstance(artifacts, list):
        return errors + ["candidate manifest artifacts must be an array"]
    matches = [
        artifact
        for artifact in artifacts
        if isinstance(artifact, dict)
        and artifact.get("format") == record.get("pluginFormat")
    ]
    if len(matches) != 1 or not _is_hex(matches[0].get("sha256")):
        errors.append("candidate manifest must contain one matching artifact digest")
    elif matches[0]["sha256"] != actual_sha256:
        errors.append("installed artifact hash does not match candidate manifest")
    return errors


def _evidence_errors(
    evidence: JsonValue, evidence_root: Path
) -> tuple[list[str], dict[str, str]]:
    errors: list[str] = []
    hashes: dict[str, str] = {}
    if not isinstance(evidence, list) or not evidence:
        return ["PASS record requires evidence paths"], hashes
    root = evidence_root.resolve()
    for relative in evidence:
        if not isinstance(relative, str) or not relative:
            errors.append("evidence paths must be non-empty strings")
            continue
        raw_candidate = root / relative
        if raw_candidate.is_symlink():
            errors.append(f"evidence path cannot be a symbolic link: {relative}")
            continue
        candidate = raw_candidate.resolve()
        try:
            candidate.relative_to(root)
        except ValueError:
            errors.append(f"evidence path escapes evidence root: {relative}")
            continue
        if not candidate.is_file() or candidate.stat().st_size == 0:
            errors.append(f"evidence file does not exist or is empty: {relative}")
            continue
        hashes[relative] = _sha256(candidate)
    return errors, hashes


def validate_record(
    record: JsonObject,
    evidence_root: Path,
    candidate_manifest: JsonObject,
    installed_root: Path,
) -> list[str]:
    errors: list[str] = []
    result = record.get("runtimeResult")
    if result not in RUNTIME_RESULTS:
        return [f"runtimeResult must be one of {sorted(RUNTIME_RESULTS)}"]
    target_id = record.get("targetId")
    if not isinstance(target_id, str) or not target_id:
        errors.append("targetId must be a non-empty string")
    checks = record.get("checks", {})
    evidence = record.get("evidence", [])
    if result != "PASS":
        if checks:
            errors.append("checks must be empty unless runtimeResult is PASS")
        if evidence:
            errors.append("evidence must be empty unless runtimeResult is PASS")
        return errors
    missing = sorted(REQUIRED_PASS_FIELDS - set(record))
    if missing:
        return errors + [f"PASS record missing fields: {', '.join(missing)}"]

    artifact_errors, actual_sha256 = _installed_artifact(record, installed_root)
    errors.extend(artifact_errors)
    if actual_sha256 is not None:
        errors.extend(_candidate_errors(record, candidate_manifest, actual_sha256))

    tool_identity = record.get("toolIdentity")
    if (
        not isinstance(tool_identity, dict)
        or not isinstance(tool_identity.get("name"), str)
        or not tool_identity["name"]
        or not isinstance(tool_identity.get("version"), str)
        or not tool_identity["version"]
        or not _is_hex(tool_identity.get("sha256"))
    ):
        errors.append("toolIdentity must include name, version, and SHA-256")
    if not isinstance(checks, dict):
        errors.append("checks must be an object")
    else:
        missing_checks = sorted(CHECK_NAMES - set(checks))
        if missing_checks:
            errors.append(f"PASS record missing checks: {', '.join(missing_checks)}")
        for name, value in checks.items():
            if name not in CHECK_NAMES:
                errors.append(f"unknown check: {name}")
            if value != "PASS":
                errors.append(f"PASS record requires {name}=PASS")

    evidence_failures, evidence_hashes = _evidence_errors(evidence, evidence_root)
    errors.extend(evidence_failures)
    for name in ("osVersion", "hostVersion", "pluginFormat", "executedAt", "executor"):
        if not isinstance(record.get(name), str) or not record[name].strip():
            errors.append(f"{name} must be a non-empty string")
    declared_hashes = record.get("evidenceSha256")
    if declared_hashes is not None and declared_hashes != evidence_hashes:
        errors.append("evidenceSha256 does not match the actual evidence files")
    if not errors and actual_sha256 is not None:
        record["pluginSha256"] = actual_sha256
        record["artifactTreeSha256"] = actual_sha256
        record["candidateManifestSha256"] = _json_sha256(candidate_manifest)
        record["evidenceSha256"] = evidence_hashes
    return errors
