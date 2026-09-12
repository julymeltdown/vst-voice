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


def validate_source_strategy_draft(document: dict[str, Any], root: Path) -> ProductionResult:
    """Validate editable source choices without asserting acquisition/release readiness.

    Unlike schema 1's comparison/qualification document, schema 2 permits no
    selected source and unavailable evidence while work is still a draft.
    Permission flags remain explicit; this function never grants execution.
    """
    del root  # Planned locators need not exist until execution admission.
    errors: list[str] = []
    if not isinstance(document, dict):
        return ProductionResult(False, ("source draft must be an object",), ())
    if type(document.get("schemaVersion")) is not int or document["schemaVersion"] != 2:
        errors.append("source draft schemaVersion must be 2")
    if document.get("status") != "DRAFT" or document.get("assetAdmissionStatus") != "NOT_RUN":
        errors.append("source draft must remain DRAFT with assetAdmissionStatus NOT_RUN")
    selected = document.get("selectedStrategyId")
    if not isinstance(selected, str):
        errors.append("source draft selectedStrategyId must be a string, empty when undecided")
    strategies = document.get("strategies")
    if not isinstance(strategies, list) or len(strategies) > 4096:
        return ProductionResult(False, tuple(errors + ["source draft strategies must be a bounded array"]), ())
    seen: set[str] = set()
    for index, strategy in enumerate(strategies):
        label = f"strategies[{index}]"
        if not isinstance(strategy, dict):
            errors.append(f"{label} must be an object")
            continue
        identity = strategy.get("id")
        if not isinstance(identity, str) or not identity or identity in seen:
            errors.append(f"{label}.id is missing or duplicated")
        else:
            seen.add(identity)
        if not isinstance(strategy.get("kind"), str) or strategy["kind"] not in STRATEGY_KINDS:
            errors.append(f"{label}.kind is invalid")
        for field in ("rights", "coverage", "listening"):
            if not isinstance(strategy.get(field), str) or strategy[field] not in FEASIBILITY:
                errors.append(f"{label}.{field} is invalid")
        permissions = strategy.get("permissions")
        if not isinstance(permissions, dict) or set(permissions) != set(PERMISSIONS):
            errors.append(f"{label}.permissions must declare exactly the four applicable permissions")
        elif any(type(permissions[field]) is not bool for field in PERMISSIONS):
            errors.append(f"{label}.permissions must contain booleans")
        locator, digest = strategy.get("licenseLocator"), strategy.get("licenseSha256")
        if not isinstance(locator, str) or not isinstance(digest, str) or not isinstance(strategy.get("evidenceState"), str):
            errors.append(f"{label} evidence fields must be explicit strings")
            continue
        if locator and ("\\" in locator or "\0" in locator or locator.startswith("/") or
                        any(part in {"", ".", ".."} for part in locator.split("/"))):
            errors.append(f"{label}.licenseLocator must be a safe repository-relative draft locator")
        if digest and (not is_hex_digest(digest) or digest != digest.lower() or not locator):
            errors.append(f"{label}.licenseSha256 must identify its declared evidence or be empty")
    if isinstance(selected, str) and selected and selected not in seen:
        errors.append("source draft selectedStrategyId does not identify a declared strategy")
    return ProductionResult(not errors, tuple(errors), ())


def validate_source_execution(document: dict[str, Any], root: Path) -> ProductionResult:
    """Admit schema-2 draft execution, never redistribution or musical quality."""
    structural = validate_source_strategy_draft(document, root)
    if not structural.passed:
        return structural
    errors: list[str] = []
    selected = next((item for item in document["strategies"] if item["id"] == document["selectedStrategyId"]), None)
    if selected is None:
        return ProductionResult(False, ("Select an authorized source before execution",), ())
    if selected["rights"] != "PASS" or not selected["permissions"]["sourceUse"] or not selected["permissions"]["transformation"]:
        errors.append("source execution requires source-use and transformation authorization")
    if not is_hex_digest(selected["licenseSha256"]):
        errors.append("source execution requires a captured evidence digest")
    evidence = _evidence_path(root, selected["licenseLocator"], "selected source", errors)
    if evidence is not None:
        try:
            if evidence.stat().st_size > 4 * 1024 * 1024:
                errors.append("source execution evidence exceeds 4 MiB")
            elif sha256_file(evidence) != selected["licenseSha256"]:
                errors.append("source execution evidence changed")
        except OSError as exc:
            errors.append(f"source execution evidence cannot be read: {exc}")
    return ProductionResult(not errors, tuple(errors), ())
