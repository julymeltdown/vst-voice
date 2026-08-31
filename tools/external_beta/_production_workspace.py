from __future__ import annotations

import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any

from tools.voicebank_script_generator import production_assignments, validate_inventory

from ._production_common import ProductionResult, is_hex_digest, is_timestamp, sha256_file
from ._source_admission import validate_source_strategy_document


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


def _pretty_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n"


def _write_new(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(text.encode("utf-8"))
        stream.flush()
        os.fsync(stream.fileno())


def _project_strategy(item: dict[str, Any], root: Path) -> dict[str, Any]:
    return {
        "id": item["id"],
        "kind": item["kind"],
        "rights": item["rights"],
        "coverage": item["coverage"],
        "listening": item["listening"],
        "permissions": dict(item["permissions"]),
        "licenseLocator": str((root / item["licenseLocator"]).resolve()),
        "licenseSha256": item["licenseSha256"],
        "evidenceState": item["evidenceState"],
    }


def initialize_production_workspace(
    workspace: Path,
    inventory: dict[str, Any],
    strategies: dict[str, Any],
    *,
    project_id: str,
    operator_id: str,
    occurred_at: str,
    repository_root: Path = REPOSITORY_ROOT,
) -> dict[str, Any]:
    inventory_errors = validate_inventory(inventory)
    if inventory_errors:
        raise ValueError("invalid production inventory: " + "; ".join(inventory_errors))
    strategy_result = validate_source_strategy_document(strategies, repository_root)
    if not strategy_result.passed:
        raise ValueError("invalid source strategy document: " + "; ".join(strategy_result.errors))
    if not isinstance(project_id, str) or not project_id or not isinstance(operator_id, str) or not operator_id:
        raise ValueError("project_id and operator_id are required")
    if not isinstance(occurred_at, str) or not occurred_at.endswith("Z") or not is_timestamp(occurred_at):
        raise ValueError("occurred_at must be a UTC timestamp ending in Z")
    selected = next(item for item in strategies["strategies"] if item["id"] == strategies["selectedStrategyId"])
    project = {
        "format": "com.project-seam.voicebank-production",
        "schemaVersion": 1,
        "projectId": project_id,
        "inventoryId": inventory["profileId"],
        "inventorySha256": inventory["inventorySha256"],
        "selectedSourceStrategyId": selected["id"],
        "licenseLocator": str((repository_root / selected["licenseLocator"]).resolve()),
        "licenseSha256": selected["licenseSha256"],
        "immutableAssetRoot": "assets",
        "sourceStrategies": [_project_strategy(item, repository_root) for item in strategies["strategies"]],
        "assets": [],
        "takes": [],
        "derivedRevisions": [],
        "metadataRevisions": [],
        "unitAssignments": production_assignments(inventory),
        "operators": [{"operatorId": operator_id, "role": "PRODUCER"}],
        "reviews": [],
        "lastDurableGeneration": 1,
    }
    project_text = _pretty_json(project)
    project_digest = hashlib.sha256(project_text.encode("utf-8")).hexdigest()
    journal = {
        "format": "com.project-seam.voicebank-production-journal-event",
        "schemaVersion": 1,
        "generation": 1,
        "projectSha256": project_digest,
        "action": "create",
        "subjectId": project_id,
        "operatorId": operator_id,
        "occurredAtUtc": occurred_at,
    }
    workspace = workspace.resolve()
    workspace.parent.mkdir(parents=True, exist_ok=True)
    if workspace.exists():
        raise FileExistsError(f"production workspace already exists: {workspace}")
    staging = Path(tempfile.mkdtemp(prefix=f".{workspace.name}.init-", dir=workspace.parent))
    try:
        for directory in ("assets", "staging", "generations", "journal"):
            (staging / directory).mkdir()
        _write_new(staging / "generations/00000000000000000001.json", project_text)
        _write_new(staging / "journal/00000000000000000001.json", _pretty_json(journal))
        _write_new(staging / "project.json", project_text)
        os.replace(staging, workspace)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return project


def _load_object(path: Path, label: str, errors: list[str]) -> tuple[dict[str, Any] | None, bytes | None]:
    try:
        if path.is_symlink() or not path.is_file():
            errors.append(f"{label} must be a regular non-symlink file")
            return None, None
        payload = path.read_bytes()
        value = json.loads(payload)
        if not isinstance(value, dict):
            errors.append(f"{label} must contain a JSON object")
            return None, payload
        return value, payload
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        errors.append(f"{label} cannot be loaded: {exc}")
        return None, None


def _validate_asset(workspace: Path, asset: Any, label: str, errors: list[str]) -> None:
    if not isinstance(asset, dict) or not is_hex_digest(asset.get("sha256")):
        errors.append(f"{label} is invalid")
        return
    relative_text = asset.get("relativePath")
    if not isinstance(relative_text, str) or not relative_text or "\\" in relative_text:
        errors.append(f"{label}.relativePath is unsafe")
        return
    relative = PurePosixPath(relative_text)
    if relative.is_absolute() or any(part in {"", ".", ".."} for part in relative.parts):
        errors.append(f"{label}.relativePath is unsafe")
        return
    path = workspace / "assets" / relative_text
    try:
        if path.is_symlink() or not path.is_file():
            errors.append(f"{label} file is unavailable")
            return
        if path.stat().st_size != asset.get("byteSize"):
            errors.append(f"{label}.byteSize does not match")
        if sha256_file(path) != asset["sha256"].lower():
            errors.append(f"{label}.sha256 does not match")
    except OSError as exc:
        errors.append(f"{label} cannot be inspected: {exc}")


def _assignments_match_inventory(actual: Any, inventory: dict[str, Any]) -> bool:
    if not isinstance(actual, list):
        return False
    expected = production_assignments(inventory)
    expected_by_key = {(item["coverageKey"], item["pitchLayer"]): item for item in expected}
    if len(actual) != len(expected_by_key):
        return False
    seen: set[tuple[str, int]] = set()
    for item in actual:
        if not isinstance(item, dict) or not isinstance(item.get("pitchLayer"), int):
            return False
        key = (item.get("coverageKey"), item["pitchLayer"])
        planned = expected_by_key.get(key)
        if planned is None or key in seen:
            return False
        if item.get("promptId") != planned["promptId"] or item.get("plannedTakeId") != planned["plannedTakeId"]:
            return False
        seen.add(key)
    return seen == set(expected_by_key)


def validate_production_workspace(
    workspace: Path,
    inventory: dict[str, Any],
    strategies: dict[str, Any],
    repository_root: Path = REPOSITORY_ROOT,
) -> ProductionResult:
    errors = list(validate_inventory(inventory))
    strategy_result = validate_source_strategy_document(strategies, repository_root)
    errors.extend(strategy_result.errors)
    if workspace.is_symlink() or not workspace.is_dir():
        errors.append("workspace must be a real directory")
    for name in ("assets", "staging", "generations", "journal"):
        path = workspace / name
        if path.is_symlink() or not path.is_dir():
            errors.append(f"workspace.{name} must be a real directory")
    generations = {path.stem: path for path in (workspace / "generations").glob("[0-9]" * 20 + ".json")}
    journals = {path.stem: path for path in (workspace / "journal").glob("[0-9]" * 20 + ".json")}
    if not generations:
        return ProductionResult(False, tuple(errors + ["workspace has no durable generation"]), ())
    if set(generations) != set(journals):
        errors.append("generation and journal sets do not match")
    latest_key = max(generations)
    latest_payload: bytes | None = None
    for key in sorted(set(generations) & set(journals)):
        project, payload = _load_object(generations[key], f"generation[{key}]", errors)
        journal, _ = _load_object(journals[key], f"journal[{key}]", errors)
        if project is None or payload is None or journal is None:
            continue
        generation = int(key)
        if project.get("lastDurableGeneration") != generation:
            errors.append(f"generation[{key}] number does not match project")
        digest = hashlib.sha256(payload).hexdigest()
        if journal.get("generation") != generation or journal.get("projectSha256") != digest:
            errors.append(f"journal[{key}] does not bind the generation")
        if (
            journal.get("format") != "com.project-seam.voicebank-production-journal-event"
            or journal.get("schemaVersion") != 1
            or journal.get("action") not in {
                "create", "import", "transform", "marker", "retake",
                "review", "save", "candidate-export",
            }
            or not isinstance(journal.get("subjectId"), str)
            or not journal["subjectId"]
            or not isinstance(journal.get("operatorId"), str)
            or not isinstance(journal.get("occurredAtUtc"), str)
            or not journal["occurredAtUtc"].endswith("Z")
            or not is_timestamp(journal["occurredAtUtc"])
        ):
            errors.append(f"journal[{key}] fields are invalid")
        if project.get("inventorySha256") != inventory.get("inventorySha256"):
            errors.append(f"generation[{key}].inventorySha256 does not match inventory")
        if project.get("selectedSourceStrategyId") != strategies.get("selectedStrategyId"):
            errors.append(f"generation[{key}] selected strategy does not match")
        operators = project.get("operators")
        operator_ids: set[str] = set()
        if not isinstance(operators, list) or not operators:
            errors.append(f"generation[{key}].operators must be a non-empty array")
        else:
            for index, operator in enumerate(operators):
                if (
                    not isinstance(operator, dict)
                    or not isinstance(operator.get("operatorId"), str)
                    or not operator["operatorId"]
                    or not isinstance(operator.get("role"), str)
                    or not operator["role"]
                    or operator["operatorId"] in operator_ids
                ):
                    errors.append(f"generation[{key}].operators[{index}] is invalid")
                    continue
                operator_ids.add(operator["operatorId"])
        if journal.get("operatorId") not in operator_ids:
            errors.append(f"journal[{key}].operatorId is not a project operator")
        license_locator = project.get("licenseLocator")
        license_path = Path(license_locator) if isinstance(license_locator, str) else Path()
        if (
            not isinstance(license_locator, str)
            or not license_locator
            or not is_hex_digest(project.get("licenseSha256"))
            or not license_path.is_file()
            or license_path.is_symlink()
        ):
            errors.append(f"generation[{key}] license evidence is unavailable")
        elif sha256_file(license_path) != project["licenseSha256"].lower():
            errors.append(f"generation[{key}] licenseSha256 does not match")
        assets = project.get("assets")
        if not isinstance(assets, list):
            errors.append(f"generation[{key}].assets must be an array")
        else:
            for index, asset in enumerate(assets):
                _validate_asset(
                    workspace, asset, f"generation[{key}].assets[{index}]", errors
                )
        if key == latest_key:
            latest_payload = payload
            if not _assignments_match_inventory(project.get("unitAssignments"), inventory):
                errors.append("latest unitAssignments do not match the deterministic inventory")
    pointer, pointer_payload = _load_object(workspace / "project.json", "project pointer", errors)
    if pointer is None or pointer_payload != latest_payload:
        errors.append("project pointer does not match the latest durable generation")
    return ProductionResult(not errors, tuple(errors), ())
