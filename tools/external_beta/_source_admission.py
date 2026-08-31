from __future__ import annotations

from pathlib import Path, PurePosixPath
from typing import Any

from ._production_common import ProductionResult, is_hex_digest, sha256_file


STRATEGY_KINDS = {"HUMAN_RECORDING", "PROCEDURAL_SYNTHESIS", "TTS_DERIVED"}
FEASIBILITY = {"PASS", "BLOCKED", "NOT_ASSESSED"}
PERMISSIONS = (
    "sourceUse",
    "transformation",
    "singingBankRedistribution",
    "commercialRenders",
)


def _evidence_path(root: Path, locator: Any, label: str, errors: list[str]) -> Path | None:
    if not isinstance(locator, str) or not locator or "\\" in locator:
        errors.append(f"{label}.licenseLocator must be a safe repository-relative path")
        return None
    relative = PurePosixPath(locator)
    if relative.is_absolute() or any(part in {"", ".", ".."} for part in relative.parts):
        errors.append(f"{label}.licenseLocator must be a safe repository-relative path")
        return None
    try:
        resolved_root = root.resolve(strict=True)
        candidate = root / locator
        if candidate.is_symlink():
            errors.append(f"{label}.licenseLocator must not be a symbolic link")
            return None
        resolved = candidate.resolve(strict=True)
        if resolved_root != resolved and resolved_root not in resolved.parents:
            errors.append(f"{label}.licenseLocator escapes the repository root")
            return None
        if not resolved.is_file():
            errors.append(f"{label}.licenseLocator is not a regular file")
            return None
        return resolved
    except OSError as exc:
        errors.append(f"{label}.licenseLocator cannot be inspected: {exc}")
        return None


def _validate_strategy(item: Any, index: int, root: Path, errors: list[str]) -> str | None:
    label = f"strategies[{index}]"
    if not isinstance(item, dict):
        errors.append(f"{label} must be an object")
        return None
    strategy_id = item.get("id")
    if not isinstance(strategy_id, str) or not strategy_id:
        errors.append(f"{label}.id is required")
        return None
    if item.get("kind") not in STRATEGY_KINDS:
        errors.append(f"{label}.kind is invalid")
    for field in ("rights", "coverage", "listening"):
        if item.get(field) not in FEASIBILITY:
            errors.append(f"{label}.{field} is invalid")
    permissions = item.get("permissions")
    if not isinstance(permissions, dict):
        errors.append(f"{label}.permissions is required")
        permissions = {}
    for permission in PERMISSIONS:
        if not isinstance(permissions.get(permission), bool):
            errors.append(f"{label}.permissions.{permission} must be boolean")
    for field in ("availability", "evidenceState", "estimatedEffort", "coveragePlan", "listeningPlan"):
        if not isinstance(item.get(field), str) or not item[field]:
            errors.append(f"{label}.{field} is required")
    if not is_hex_digest(item.get("licenseSha256")):
        errors.append(f"{label}.licenseSha256 must be a SHA-256 digest")
    evidence = _evidence_path(root, item.get("licenseLocator"), label, errors)
    if evidence is not None and is_hex_digest(item.get("licenseSha256")):
        if sha256_file(evidence) != item["licenseSha256"].lower():
            errors.append(f"{label}.licenseSha256 does not match the evidence file")
    if item.get("rights") == "PASS":
        for permission in PERMISSIONS:
            if permissions.get(permission) is not True:
                errors.append(f"{label}.permissions.{permission} must be true when rights PASS")
    return strategy_id


def validate_source_strategy_document(document: dict[str, Any], root: Path) -> ProductionResult:
    errors: list[str] = []
    if not isinstance(document, dict):
        return ProductionResult(False, ("source strategy document must be an object",), ())
    if document.get("schemaVersion") != 1:
        errors.append("source strategy schemaVersion must be 1")
    if document.get("status") != "READY_FOR_ACQUISITION":
        errors.append("source strategy status must be READY_FOR_ACQUISITION")
    if document.get("assetAdmissionStatus") != "NOT_RUN":
        errors.append("assetAdmissionStatus must remain NOT_RUN before U57")
    if not isinstance(document.get("realAssetWarning"), str) or not document["realAssetWarning"]:
        errors.append("realAssetWarning is required")
    strategies = document.get("strategies")
    if not isinstance(strategies, list) or not strategies:
        return ProductionResult(False, tuple(errors + ["strategies must be a non-empty array"]), ())
    by_id: dict[str, dict[str, Any]] = {}
    seen_kinds: set[str] = set()
    for index, item in enumerate(strategies):
        strategy_id = _validate_strategy(item, index, root, errors)
        if strategy_id is None or not isinstance(item, dict):
            continue
        if strategy_id in by_id:
            errors.append(f"strategies[{index}].id is duplicated")
        else:
            by_id[strategy_id] = item
        if isinstance(item.get("kind"), str):
            seen_kinds.add(item["kind"])
    for missing in sorted(STRATEGY_KINDS - seen_kinds):
        errors.append(f"source strategy comparison is missing {missing}")
    selected_id = document.get("selectedStrategyId")
    selected = by_id.get(selected_id) if isinstance(selected_id, str) else None
    if selected is None:
        errors.append("selectedStrategyId does not identify a strategy")
    else:
        for field in ("rights", "coverage", "listening"):
            if selected.get(field) != "PASS":
                errors.append(f"selected strategy {field} must be PASS")
        permissions = selected.get("permissions") if isinstance(selected.get("permissions"), dict) else {}
        for permission in PERMISSIONS:
            if permissions.get(permission) is not True:
                errors.append(f"selected strategy permissions.{permission} must be true")
    return ProductionResult(not errors, tuple(errors), ())
