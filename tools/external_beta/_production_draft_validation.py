"""Bounded source-aware producer persistence checks, not release qualification."""
from __future__ import annotations

import hashlib
import json
import math
import os
import stat
from pathlib import Path
from typing import Any

from ._production_common import ProductionResult, is_hex_digest, is_timestamp, sha256_file
from ._source_admission import FEASIBILITY, PERMISSIONS, STRATEGY_KINDS
from ._production_inventory import inventory_errors, producer_assignments, style_owned

MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_HISTORY_BYTES = 256 * 1024 * 1024
MAX_JOURNAL_BYTES = 1024 * 1024
MAX_ABORTED_JOURNAL_BYTES = 16 * 1024 * 1024
JOURNAL_ACTIONS = {
    "create", "import", "transform", "marker", "retake", "review", "save", "candidate-export",
    "import-procedural", "import-generated-batch",
    "source-quality-assessment", "source-register",
}
QUEUE_STATES = {"MISSING", "REJECTED", "RETAKE", "MARKER_REVIEW", "PITCH_REVIEW", "APPROVED"}


def _integer(value: Any, minimum: int = 0, maximum: int = (1 << 63) - 1) -> bool:
    return type(value) is int and minimum <= value <= maximum


def _utc(value: Any) -> bool:
    return isinstance(value, str) and value.endswith("Z") and is_timestamp(value)


def _read_bounded_bytes(path: Path, maximum_bytes: int) -> bytes:
    if maximum_bytes < 0 or path.is_symlink():
        raise ValueError(f"{path} must be a bounded regular non-symlink file")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_size > maximum_bytes:
            raise ValueError(f"{path} must be a bounded regular non-symlink file")
        with os.fdopen(descriptor, "rb", closefd=False) as stream:
            payload = stream.read(maximum_bytes + 1)
        if len(payload) > maximum_bytes:
            raise ValueError(f"{path} grew beyond its byte limit")
        return payload
    finally:
        os.close(descriptor)


def read_bounded_object(path: Path, maximum_bytes: int = MAX_JSON_BYTES) -> tuple[dict[str, Any], bytes]:
    payload = _read_bounded_bytes(path, maximum_bytes)

    def unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON field: {key}")
            result[key] = value
        return result

    def nonfinite(value: str) -> Any:
        raise ValueError(f"non-finite JSON number: {value}")

    value = json.loads(payload, object_pairs_hook=unique, parse_constant=nonfinite)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    pending: list[tuple[Any, int]] = [(value, 0)]
    nodes = 0
    while pending:
        item, depth = pending.pop()
        nodes += 1
        if depth > 24 or nodes > 1_000_000:
            raise ValueError(f"{path} exceeds producer JSON depth/node bounds")
        if isinstance(item, dict):
            pending.extend((child, depth + 1) for child in item.values())
        elif isinstance(item, list):
            pending.extend((child, depth + 1) for child in item)
        elif isinstance(item, float) and not math.isfinite(item):
            raise ValueError(f"{path} contains a non-finite number")
        elif isinstance(item, str) and len(item.encode("utf-8")) > 8 * 1024 * 1024:
            raise ValueError(f"{path} exceeds producer JSON string bounds")
    return value, payload


def _verify_ancestry(workspace: Path, journal: dict[str, Any], generation: int,
                     previous_generation: int, previous_payload: bytes, previous_journal: bytes,
                     remaining_bytes: int) -> tuple[set[str], int]:
    """Read-only counterpart of repository_history_internal::verifyAncestry.

    An abort is a journal-only attempt certified by the next committed child,
    never an inference from a missing numbered file. Legacy history still has
    to be contiguous, and its source qualification contract is unchanged.
    """
    if "ancestry" not in journal:
        if generation != previous_generation + 1:
            raise ValueError("source-aware history must be contiguous from generation 1 or certify aborted writes")
        return set(), 0
    ancestry = journal["ancestry"]
    if not isinstance(ancestry, dict) or set(ancestry) != {
        "format", "parentGeneration", "parentProjectSha256", "parentJournalSha256", "abortedGenerations"
    } or ancestry.get("format") != "parent-and-aborted-journals-v1":
        raise ValueError("production journal ancestry fields are invalid")
    parent = ancestry["parentGeneration"]
    aborted = ancestry["abortedGenerations"]
    if not _integer(parent) or parent != previous_generation or parent >= generation or not isinstance(aborted, list) or (
        len(aborted) > 1024 or generation - parent - 1 != len(aborted)
    ):
        raise ValueError("production ancestry does not name the previous verified committed generation")
    project_hash, journal_hash = ancestry["parentProjectSha256"], ancestry["parentJournalSha256"]
    if parent == 0:
        if generation != 1 or project_hash != "" or journal_hash != "":
            raise ValueError("initial generation ancestry must have an empty parent")
    elif project_hash != hashlib.sha256(previous_payload).hexdigest() or journal_hash != hashlib.sha256(previous_journal).hexdigest():
        raise ValueError("production ancestry parent bytes have changed or are missing")
    certified: set[str] = set()
    byte_count = 0
    for expected, entry in enumerate(aborted, parent + 1):
        if not isinstance(entry, dict) or set(entry) != {"generation", "journalSha256", "journalBytes"}:
            raise ValueError("aborted-write evidence fields are invalid")
        number, digest, size = entry["generation"], entry["journalSha256"], entry["journalBytes"]
        if not _integer(number, 1) or number != expected or not is_hex_digest(digest) or digest != digest.lower() or (
            not _integer(size, 0, MAX_JOURNAL_BYTES) or size > MAX_ABORTED_JOURNAL_BYTES - byte_count
        ):
            raise ValueError("aborted-write evidence identity or byte budget is invalid")
        name = f"{number:020}.json"
        snapshot = workspace / "generations" / name
        if snapshot.is_symlink() or snapshot.exists():
            raise ValueError("aborted-write evidence cannot replace a committed generation snapshot")
        payload = _read_bounded_bytes(workspace / "journal" / name,
                                      min(MAX_JOURNAL_BYTES, remaining_bytes - byte_count))
        if len(payload) != size or hashlib.sha256(payload).hexdigest() != digest:
            raise ValueError("aborted-write journal bytes have changed")
        byte_count += len(payload)
        certified.add(name)
    return certified, byte_count


def _history_entries(directory: Path) -> list[Path]:
    result: list[Path] = []
    for path in directory.iterdir():
        name = path.name
        if len(result) >= 65536 or len(name) != 25 or not name.endswith(".json") or not name[:20].isascii() or not name[:20].isdigit():
            raise ValueError(f"{directory} contains unrecognized or excessive history entries")
        result.append(path)
    return sorted(result)


def _file(root: Path, relative: Any, digest: Any, limit: int, label: str, errors: list[str]) -> bool:
    if not isinstance(relative, str) or not relative or relative.startswith("/") or "\\" in relative or "\0" in relative or any(
        part in {"", ".", ".."} for part in relative.split("/")
    ) or not is_hex_digest(digest):
        errors.append(f"{label} path/digest is invalid")
        return False
    try:
        path = root
        for part in relative.split("/"):
            path /= part
            if path.is_symlink():
                errors.append(f"{label} contains a symbolic-link component")
                return False
        if not path.is_file() or path.stat().st_size > limit:
            errors.append(f"{label} is missing or exceeds its byte limit")
            return False
        elif sha256_file(path) != digest.lower():
            errors.append(f"{label} digest does not match retained bytes")
            return False
    except OSError as exc:
        errors.append(f"{label} cannot be inspected: {exc}")
        return False
    return True


def _records(project: dict[str, Any], field: str, identity: str, label: str, errors: list[str]) -> dict[str, dict[str, Any]]:
    values = project.get(field)
    if not isinstance(values, list) or len(values) > 500_000:
        errors.append(f"{label}.{field} must be a bounded array")
        return {}
    result: dict[str, dict[str, Any]] = {}
    for item in values:
        if not isinstance(item, dict) or not isinstance(item.get(identity), str) or not item[identity] or item[identity] in result:
            errors.append(f"{label}.{field} has a malformed or duplicate identity")
            continue
        result[item[identity]] = item
    return result


def _strategy(strategy: dict[str, Any], label: str, errors: list[str]) -> None:
    if not isinstance(strategy.get("id"), str) or not strategy["id"] or not isinstance(strategy.get("kind"), str) or strategy["kind"] not in STRATEGY_KINDS:
        errors.append(f"{label} source identity/kind is invalid")
    for field in ("rights", "coverage", "listening"):
        if not isinstance(strategy.get(field), str) or strategy[field] not in FEASIBILITY:
            errors.append(f"{label}.{field} is invalid")
    permissions = strategy.get("permissions")
    if not isinstance(permissions, dict) or set(permissions) != set(PERMISSIONS) or any(type(value) is not bool for value in permissions.values()):
        errors.append(f"{label}.permissions must contain the four explicit booleans")
    locator, digest = strategy.get("licenseLocator"), strategy.get("licenseSha256")
    if not isinstance(locator, str) or not isinstance(digest, str) or not isinstance(strategy.get("evidenceState"), str):
        errors.append(f"{label} source evidence fields must be strings")
    elif digest and (not locator or not is_hex_digest(digest)):
        errors.append(f"{label} source evidence identity is invalid")


def _execution(strategy: Any) -> bool:
    if not isinstance(strategy, dict):
        return False
    permissions = strategy.get("permissions")
    return isinstance(permissions, dict) and strategy.get("rights") == "PASS" and permissions.get("sourceUse") is True and permissions.get("transformation") is True and bool(strategy.get("licenseLocator")) and is_hex_digest(strategy.get("licenseSha256"))


def _qualified(snapshot: Any, current: Any) -> bool:
    return _execution(snapshot) and _execution(current) and snapshot.get("id") == current.get("id") and snapshot.get("kind") == current.get("kind") and snapshot.get("licenseSha256") == current.get("licenseSha256") and all(
        snapshot["permissions"].get(field) is True and current["permissions"].get(field) is True
        for field in ("singingBankRedistribution", "commercialRenders")
    ) and current.get("coverage") == "PASS" and current.get("listening") == "PASS"


def _quality_hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def _quality_policy_identity(source: dict[str, Any]) -> str:
    return _quality_hash({"format": "seam-source-quality-policy-v1", **{key: source[key] for key in
        ("id", "kind", "rights", "licenseLocator", "licenseSha256")}, **source["permissions"]})


def _quality_material_identity(project: dict[str, Any], strategy: str) -> str:
    takes = {row["takeId"]: row for row in project["takes"]}
    sources = {row["id"]: row for row in project["sourceBindings"]}
    revisions = {row["revisionId"]: row for row in project["derivedRevisions"]}
    rows: dict[str, dict[str, Any]] = {}
    for assignment in project["unitAssignments"]:
        if not assignment["takeId"]:
            continue
        take = takes[assignment["takeId"]]
        source = sources.get(take["sourceBindingId"])
        if source is None or source["strategy"]["id"] != strategy:
            continue
        audio, parent, editors = take["rawAssetSha256"], "", []
        for identity in take["derivedRevisionIds"]:
            revision = revisions[identity]
            if revision["inputSha256"] != audio:
                raise ValueError("assessed processing chain is broken")
            audio, parent = revision["outputSha256"], identity
            editors.append(revision["operatorId"])
        if take["takeId"] in rows:
            raise ValueError("assessed take is assigned more than once")
        rows[take["takeId"]] = {"takeId": take["takeId"], "sourceBindingId": source["id"],
            "coverageKey": assignment["coverageKey"], "pitchLayer": assignment["pitchLayer"],
            "promptId": assignment["promptId"], "rawSha256": take["rawAssetSha256"], "effectiveSha256": audio,
            "parentRevisionId": parent, "importerId": source["importerId"], "editors": editors}
        if project["schemaVersion"] >= 4:
            rows[take["takeId"]].update(language=project["language"], style=assignment["style"])
    if not rows:
        raise ValueError("assessment has no active source-owned material")
    return _quality_hash({"format": "seam-source-quality-material-v1", "projectId": project["projectId"],
        "inventoryId": project["inventoryId"], "inventorySha256": project["inventorySha256"],
        "strategyId": strategy, "takes": [rows[key] for key in sorted(rows)]})


def _quality_current(project: dict[str, Any], strategy: str) -> bool:
    history = project.get("sourceQualityAssessments", [])
    if not isinstance(history,list) or any(not isinstance(row,dict) for row in history):
        return False
    row = next((row for row in reversed(history) if row.get("strategyId") == strategy), None)
    if row is None:
        return True
    try:
        source = next(row for row in project["sourceStrategies"] if row["id"] == strategy)
        return (row["policySha256"] == _quality_policy_identity(source) and
                row["materialSha256"] == _quality_material_identity(project, strategy) and
                row["coverage"] == source["coverage"] == "PASS" and row["listening"] == source["listening"] == "PASS")
    except (KeyError, TypeError, ValueError, StopIteration):
        return False


def _quality_reviewer_independent(project: dict[str, Any], row: dict[str, Any]) -> bool:
    try:
        reviewer = row["reviewerId"]
        if not any(actor["operatorId"] == reviewer and actor["role"] == "REVIEWER" for actor in project["operators"]):
            return False
        active = {assignment["takeId"] for assignment in project["unitAssignments"]}
        takes = {take["takeId"]: take for take in project["takes"]}
        revisions = {revision["revisionId"]: revision for revision in project["derivedRevisions"]}
        for binding in project["sourceBindings"]:
            if binding["strategy"]["id"] != row["strategyId"] or binding["takeId"] not in active:
                continue
            if binding["importerId"] == reviewer or any(revisions[identity]["operatorId"] == reviewer for identity in takes[binding["takeId"]]["derivedRevisionIds"]):
                return False
        return True
    except (KeyError, TypeError):
        return False


def _project(root: Path, project: dict[str, Any], label: str, errors: list[str]) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    schema = project.get("schemaVersion")
    if type(schema) is not int or schema not in (1, 2, 3, 4) or project.get("format") != "com.project-seam.voicebank-production":
        errors.append(f"{label} producer schema is invalid")
        return {}, {}
    if (schema == 4 and project.get("language") not in ("ja", "en", "ko")) or (schema < 4 and "language" in project):
        errors.append(f"{label} language ownership does not match its schema")
    def valid_style(row):
        if schema < 4:
            return "style" not in row
        value = row.get("style")
        return isinstance(value, str) and 0 < len(value.encode("utf-8")) <= 128 and not any(ord(c) < 32 or ord(c) == 127 for c in value)
    if not isinstance(project.get("projectId"), str) or not project["projectId"]:
        errors.append(f"{label}.projectId is required")
    if not isinstance(project.get("immutableAssetRoot"), str) or not project["immutableAssetRoot"] or any(
        part in project["immutableAssetRoot"] for part in ("/", "\\")
    ) or project["immutableAssetRoot"] in (".", ".."):
        errors.append(f"{label}.immutableAssetRoot is invalid")
    for field in ("inventoryId", "inventorySha256", "selectedSourceStrategyId", "licenseLocator", "licenseSha256"):
        if not isinstance(project.get(field), str):
            errors.append(f"{label}.{field} must be an explicit string")
    if bool(project.get("inventoryId")) != bool(project.get("inventorySha256")) or (
        project.get("inventorySha256") and not is_hex_digest(project["inventorySha256"])
    ):
        errors.append(f"{label} inventory identity is incomplete")
    operators = _records(project, "operators", "operatorId", label, errors)
    if not operators or any(not isinstance(value.get("role"), str) or not value["role"] for value in operators.values()):
        errors.append(f"{label}.operators must declare roles")
    strategies = _records(project, "sourceStrategies", "id", label, errors)
    for strategy in strategies.values():
        _strategy(strategy, f"{label}.sourceStrategies[{strategy['id']}]", errors)
    assessments = _records(project,"sourceQualityAssessments","id",label,errors) if schema >= 3 else {}
    if (schema < 3 and "sourceQualityAssessments" in project) or len(assessments) > 1024:
        errors.append(f"{label} source quality history schema/bounds are invalid")
        assessments = {}
    for row in assessments.values():
        if set(row) != {"id","strategyId","policySha256","materialSha256","evidenceSha256","reviewerId","reviewedAtUtc","coverage","listening"} or any(not isinstance(value,str) for value in row.values()) or (
            len(row["id"]) > 128 or row.get("strategyId") not in strategies or
            any(not is_hex_digest(row.get(key)) for key in ("policySha256","materialSha256","evidenceSha256")) or
            not _utc(row.get("reviewedAtUtc")) or operators.get(row.get("reviewerId"),{}).get("role") != "REVIEWER" or
            row.get("coverage") not in FEASIBILITY or row.get("listening") not in FEASIBILITY
        ):
            errors.append(f"{label} source quality assessment fields/reviewer are invalid")
            continue
        _file(root,f"source-evidence/{row['evidenceSha256']}.quality.txt",row["evidenceSha256"],4*1024*1024,f"{label}.sourceQualityAssessments",errors)
    selected_id = project.get("selectedSourceStrategyId")
    selected = strategies.get(selected_id) if isinstance(selected_id, str) else None
    if selected_id and (selected is None or selected.get("licenseLocator") != project.get("licenseLocator") or selected.get("licenseSha256") != project.get("licenseSha256")):
        errors.append(f"{label} selected source evidence is not bound to the project")
    elif not selected_id and (project.get("licenseLocator") or project.get("licenseSha256")):
        errors.append(f"{label} unselected draft must not attribute source evidence")
    if schema == 1:
        if "sourceBindings" in project or "lifecycle" in project or not _qualified(selected, selected) or not project.get("inventoryId"):
            errors.append(f"{label} legacy producer qualification/schema contract is invalid")
        locator = project.get("licenseLocator")
        if isinstance(locator, str) and locator:
            path = Path(locator)
            if path.is_symlink() or not path.is_file() or sha256_file(path) != project.get("licenseSha256", "").lower():
                errors.append(f"{label} legacy source evidence is missing or changed")
    elif project.get("lifecycle") not in ("DRAFT", "EXPERIMENTAL", "QUALIFIED"):
        errors.append(f"{label} draft lifecycle is invalid")
    assets = _records(project, "assets", "sha256", label, errors)
    for digest, asset in assets.items():
        if not _integer(asset.get("byteSize"), 1) or asset.get("kind") not in ("RAW", "DERIVED"):
            errors.append(f"{label} asset size/kind is invalid")
        relative = asset.get("relativePath")
        if isinstance(relative, str):
            valid_file = _file(root, "assets/" + relative, digest, 512 * 1024 * 1024, f"{label}.asset[{digest}]", errors)
            path = root / "assets" / relative
            if valid_file and path.stat().st_size != asset.get("byteSize"):
                errors.append(f"{label} immutable asset byteSize does not match")
        else:
            errors.append(f"{label} asset path is invalid")
    takes = _records(project, "takes", "takeId", label, errors)
    revisions = _records(project, "derivedRevisions", "revisionId", label, errors)
    metadata = _records(project, "metadataRevisions", "revisionId", label, errors)
    reviews = _records(project, "reviews", "reviewId", label, errors)
    bindings = _records(project, "sourceBindings", "id", label, errors) if schema >= 2 else {}
    source_takes: set[str] = set()
    for binding in bindings.values():
        if set(binding) != {"id", "takeId", "rawAssetSha256", "strategy", "importerId", "importedAtUtc", "licenseSnapshotPath"}:
            errors.append(f"{label} source binding fields do not match schema 2")
        owner = takes.get(binding.get("takeId")) if isinstance(binding.get("takeId"), str) else None
        snapshot = binding.get("strategy")
        if not isinstance(snapshot, dict):
            errors.append(f"{label} captured source strategy is missing")
            continue
        _strategy(snapshot, f"{label}.sourceBindings[{binding['id']}].strategy", errors)
        if not owner or owner.get("sourceBindingId") != binding["id"] or owner.get("rawAssetSha256") != binding.get("rawAssetSha256") or owner.get("takeId") in source_takes or not _execution(snapshot):
            errors.append(f"{label} captured source binding is not an authorized unique take ingress")
        if owner:
            source_takes.add(owner["takeId"])
        if binding.get("importerId") not in operators or not _utc(binding.get("importedAtUtc")):
            errors.append(f"{label} captured importer identity/time is invalid")
        digest = snapshot.get("licenseSha256")
        relative = binding.get("licenseSnapshotPath")
        if relative != f"source-evidence/{digest}.txt":
            errors.append(f"{label} retained source evidence path is not content-addressed")
        _file(root, relative, digest, 4 * 1024 * 1024, f"{label}.sourceBindings[{binding['id']}]", errors)
    owned_revisions: set[str] = set()
    for take in takes.values():
        if not valid_style(take) or (schema == 4 and not _integer(take.get("pitchLayer"), 24, 96)):
            errors.append(f"{label} take style/pitch ownership is invalid")
        if take.get("supersedesTakeId"):
            parent = takes.get(take["supersedesTakeId"]) if isinstance(take["supersedesTakeId"], str) else None
            if not parent or parent is take or any(parent.get(field) != take.get(field) for field in ("coverageKey", "pitchLayer", "promptId", "style")):
                errors.append(f"{label} retake identity differs from its parent")
        if any(not isinstance(take.get(field), str) or not take[field] for field in ("promptId", "coverageKey")) or not isinstance(take.get("supersedesTakeId"), str):
            errors.append(f"{label} take prompt/coverage/supersedes fields are invalid")
        if take.get("rawAssetSha256") not in assets or not _integer(take.get("pitchLayer"), -(1 << 31), (1 << 31) - 1) or take.get("state") not in QUEUE_STATES:
            errors.append(f"{label} take input/pitch/state is invalid")
        if schema == 1 and "sourceBindingId" in take:
            errors.append(f"{label} legacy take contains source-aware ownership")
        if schema >= 2 and (not isinstance(take.get("sourceBindingId"), str) or (take["sourceBindingId"] and take["takeId"] not in source_takes)):
            errors.append(f"{label} take source origin is not explicitly captured or unknown")
        previous = take.get("rawAssetSha256")
        chain = take.get("derivedRevisionIds")
        if not isinstance(chain, list):
            errors.append(f"{label} derived revision chain must be an array")
            continue
        for identity in chain:
            revision = revisions.get(identity) if isinstance(identity, str) else None
            if not revision or identity in owned_revisions or revision.get("inputSha256") != previous or revision.get("outputSha256") not in assets or revision.get("operatorId") not in operators or not _utc(revision.get("performedAtUtc")):
                errors.append(f"{label} derived revision loses take/parent ownership")
                continue
            previous = revision["outputSha256"]
            owned_revisions.add(identity)
    if owned_revisions != set(revisions):
        errors.append(f"{label} derived revisions are not owned exactly once")
    for revision in metadata.values():
        take = takes.get(revision.get("takeId")) if isinstance(revision.get("takeId"), str) else None
        values = revision.get("values")
        if not take or revision.get("rawAssetSha256") != take.get("rawAssetSha256") or revision.get("operatorId") not in operators or not _utc(revision.get("performedAtUtc")) or not isinstance(values, dict) or not values or any(not isinstance(value, str) for value in values.values()) or not isinstance(revision.get("kind"), str) or not revision["kind"]:
            errors.append(f"{label} metadata is not bound to its immutable take and operator")
    for review in reviews.values():
        if review.get("takeId") not in takes or review.get("result") not in ("PASS", "REJECTED") or not isinstance(review.get("reviewerId"), str) or not review["reviewerId"] or not _utc(review.get("reviewedAtUtc")):
            errors.append(f"{label} review record is invalid")
    assignments = project.get("unitAssignments")
    if not isinstance(assignments, list):
        errors.append(f"{label}.unitAssignments must be an array")
        assignments = []
    if assignments and not project.get("inventoryId"):
        errors.append(f"{label} assignments require an inventory identity")
    seen: set[tuple] = set()
    active: set[str] = set()
    for assignment in assignments:
        if not isinstance(assignment, dict) or not isinstance(assignment.get("coverageKey"), str) or not _integer(assignment.get("pitchLayer"), -(1 << 31), (1 << 31) - 1):
            errors.append(f"{label} assignment identity is invalid")
            continue
        if not valid_style(assignment) or (schema == 4 and not _integer(assignment.get("pitchLayer"), 24, 96)):
            errors.append(f"{label} assignment style/pitch ownership is invalid")
            continue
        if any(not isinstance(assignment.get(field), str) or not assignment[field] for field in ("coverageKey", "promptId", "plannedTakeId")) or not isinstance(assignment.get("takeId"), str) or not isinstance(assignment.get("state"), str) or assignment["state"] not in QUEUE_STATES or any(type(assignment.get(field)) is not bool for field in ("markerReviewed", "pitchReviewed")):
            errors.append(f"{label} assignment prompt/state/review fields are invalid")
        key = assignment.get("style", ""), assignment["coverageKey"], assignment["pitchLayer"]
        if key in seen:
            errors.append(f"{label} assignment identity is duplicated")
        seen.add(key)
        if assignment.get("state") == "MISSING" and assignment.get("takeId") == "":
            continue
        take = takes.get(assignment.get("takeId")) if isinstance(assignment.get("takeId"), str) else None
        if not take or any(assignment.get(field) != take.get(field) for field in ("coverageKey", "pitchLayer", "promptId", "state", "style")):
            errors.append(f"{label} active assignment differs from its take")
            continue
        active.add(take["takeId"])
        if assignment.get("state") == "APPROVED" and (assignment.get("markerReviewed") is not True or assignment.get("pitchReviewed") is not True):
            errors.append(f"{label} approved assignment lacks marker/pitch review")
        if schema >= 2 and project.get("lifecycle") == "QUALIFIED":
            source = bindings.get(take.get("sourceBindingId", ""), {}).get("strategy")
            current = strategies.get(source.get("id")) if isinstance(source, dict) else None
            latest = next((review for review in reversed(list(reviews.values())) if review.get("takeId") == take["takeId"]), None)
            if assignment.get("state") != "APPROVED" or not _qualified(source, current) or not latest or latest.get("result") != "PASS" or not _quality_current(project,source.get("id","") if isinstance(source,dict) else ""):
                errors.append(f"{label} qualified lifecycle lacks source and unit qualification")
    if any(take_id not in active and take.get("state") != "RETAKE" for take_id, take in takes.items()):
        errors.append(f"{label} take is neither active nor a retained retake")
    if schema >= 2 and project.get("lifecycle") == "QUALIFIED" and not assignments:
        errors.append(f"{label} qualified lifecycle has no required units")
    return bindings, takes


def validate_draft_workspace(workspace: Path, inventory: dict[str, Any] | None = None) -> ProductionResult:
    errors: list[str] = []
    if inventory is not None:
        errors.extend(inventory_errors(inventory))
    if workspace.is_symlink() or not workspace.is_dir():
        return ProductionResult(False, ("workspace must be a real directory",), ())
    for name in ("assets", "staging", "generations", "journal"):
        if (workspace / name).is_symlink() or not (workspace / name).is_dir():
            errors.append(f"workspace.{name} must be a real directory")
    if errors:
        return ProductionResult(False, tuple(errors), ())
    try:
        generations = _history_entries(workspace / "generations")
        journals = _history_entries(workspace / "journal")
        if not generations or len(generations) > 65536 or len(journals) > 65536:
            return ProductionResult(False, ("durable generation/journal coverage is missing or mismatched",), ())
        generation_names = {item.name for item in generations}
        journal_names = {item.name for item in journals}
        if not generation_names <= journal_names:
            return ProductionResult(False, ("durable generation/journal coverage is missing or mismatched",), ())
        certified_aborts: set[str] = set()
        previous_bindings: dict[str, dict[str, Any]] = {}
        previous_takes: dict[str, dict[str, Any]] = {}
        previous_schema = 1
        project_id: str | None = None
        history_bytes = 0
        latest_payload = b""
        latest_journal = b""
        previous_generation = 0
        latest: dict[str, Any] = {}
        for path in generations:
            project, payload = read_bounded_object(path, min(MAX_JSON_BYTES, MAX_HISTORY_BYTES - history_bytes))
            history_bytes += len(payload)
            journal, journal_payload = read_bounded_object(workspace / "journal" / path.name, min(MAX_JOURNAL_BYTES, MAX_HISTORY_BYTES - history_bytes))
            history_bytes += len(journal_payload)
            label = f"generation[{path.stem}]"
            if project_id is None:
                project_id = project.get("projectId")
            if project.get("projectId") != project_id or not _integer(project.get("lastDurableGeneration"), 1) or project["lastDurableGeneration"] != int(path.stem):
                errors.append(f"{label} project/generation identity changed")
            if journal.get("format") != "com.project-seam.voicebank-production-journal-event" or type(journal.get("schemaVersion")) is not int or journal["schemaVersion"] != 1 or not _integer(journal.get("generation"), 1) or journal["generation"] != int(path.stem) or journal.get("projectSha256") != hashlib.sha256(payload).hexdigest():
                errors.append(f"{label} journal does not bind exact generation bytes")
            aborted, abort_bytes = _verify_ancestry(workspace, journal, int(path.stem), previous_generation,
                                                    latest_payload, latest_journal, MAX_HISTORY_BYTES - history_bytes)
            if certified_aborts & aborted:
                errors.append(f"{label} duplicates aborted-write evidence")
            certified_aborts.update(aborted)
            history_bytes += abort_bytes
            if previous_generation == 0 and (journal.get("action") != "create" or journal.get("subjectId") != project_id or project.get("takes")):
                errors.append("origin history must begin with the original empty producer creation")
            if not isinstance(journal.get("action"), str) or journal["action"] not in JOURNAL_ACTIONS or not isinstance(journal.get("subjectId"), str) or not journal["subjectId"] or not _utc(journal.get("occurredAtUtc")):
                errors.append(f"{label} journal fields are invalid")
            bindings, takes = _project(workspace, project, label, errors)
            old_assessments, assessments = latest.get("sourceQualityAssessments",[]), project.get("sourceQualityAssessments",[])
            if isinstance(old_assessments,list) and isinstance(assessments,list):
                if assessments[:len(old_assessments)] != old_assessments:
                    errors.append(f"{label} source quality history is not immutable")
                added = assessments[len(old_assessments):]
                if added:
                    row = added[-1]
                    try:
                        source = next(source for source in latest["sourceStrategies"] if source["id"] == row["strategyId"])
                        if (len(added) != 1 or journal.get("action") != "source-quality-assessment" or
                            journal.get("subjectId") != row["id"] or journal.get("operatorId") != row["reviewerId"] or
                            journal.get("occurredAtUtc") != row["reviewedAtUtc"] or row["policySha256"] != _quality_policy_identity(source) or
                            row["materialSha256"] != _quality_material_identity(latest,row["strategyId"]) or
                            not _quality_reviewer_independent(latest,row)):
                            errors.append(f"{label} source assessment has no matching current reviewer journal transition")
                    except (KeyError, TypeError, ValueError, StopIteration):
                        errors.append(f"{label} source assessment transition cannot be verified")
                elif journal.get("action") == "source-quality-assessment":
                    errors.append(f"{label} assessment event adds no decision")
            operators = project.get("operators")
            if not isinstance(operators, list) or not any(isinstance(item, dict) and item.get("operatorId") == journal.get("operatorId") for item in operators):
                errors.append(f"{label} journal actor is not registered")
            schema = project.get("schemaVersion")
            if previous_generation and previous_schema == 4:
                if project.get("language") != latest.get("language"):
                    errors.append(f"{label} rewrites immutable workspace language")
                for identity, take in previous_takes.items():
                    if identity not in takes or any(takes[identity].get(field) != take.get(field) for field in ("style", "coverageKey", "pitchLayer")):
                        errors.append(f"{label} reassigns immutable take production identity")
            if previous_generation and previous_schema < 4 and schema == 4:
                errors.append(f"{label} legacy migration is not yet admitted")
            if type(schema) is int and schema < previous_schema:
                errors.append(f"{label} downgrades source-aware history")
            if type(schema) is int:
                previous_schema = schema
            for identity, binding in previous_bindings.items():
                if bindings.get(identity) != binding:
                    errors.append(f"{label} rewrites or removes immutable source binding {identity}")
            for identity, take in previous_takes.items():
                if take.get("sourceBindingId") and (identity not in takes or takes[identity].get("sourceBindingId") != take["sourceBindingId"] or takes[identity].get("rawAssetSha256") != take.get("rawAssetSha256")):
                    errors.append(f"{label} reassigns immutable take source ownership")
            for identity, binding in bindings.items():
                if identity in previous_bindings:
                    continue
                if binding.get("takeId") in previous_takes or binding.get("importerId") != journal.get("operatorId") or binding.get("importedAtUtc") != journal.get("occurredAtUtc") or journal.get("action") not in {"import", "import-procedural", "import-generated-batch", "retake"}:
                    errors.append(f"{label} new source binding has no original attributed import")
                if journal.get("action") != "import-generated-batch" and journal.get("subjectId") != binding.get("takeId"):
                    errors.append(f"{label} source binding differs from its imported take")
                selected = next((item for item in project.get("sourceStrategies", []) if isinstance(item, dict) and item.get("id") == project.get("selectedSourceStrategyId")), None)
                if binding.get("strategy") != selected:
                    errors.append(f"{label} imported source snapshot differs from its capture-time strategy")
            previous_bindings, previous_takes = bindings, takes
            latest_payload, latest = payload, project
            latest_journal, previous_generation = journal_payload, int(path.stem)
        if journal_names != generation_names | certified_aborts:
            errors.append("journal-only history contains an uncertified aborted or missing committed generation")
        _, pointer = read_bounded_object(workspace / "project.json")
        if pointer != latest_payload:
            errors.append("project pointer does not match the latest exact durable generation")
        if inventory is not None:
            if latest.get("inventorySha256") != inventory.get("inventorySha256"):
                errors.append("latest inventory identity differs from the requested inventory")
            actual = latest.get("unitAssignments")
            expected = producer_assignments(inventory)
            if style_owned(inventory) and latest.get("language") != inventory["language"]:
                errors.append("latest language differs from the requested inventory")
            if not isinstance(actual, list) or len(actual) != len(expected) or sorted(
                (item.get("style", ""), item.get("coverageKey"), item.get("pitchLayer"), item.get("promptId"), item.get("plannedTakeId")) for item in actual if isinstance(item, dict)
            ) != sorted((item.get("style", ""), item["coverageKey"], item["pitchLayer"], item["promptId"], item["plannedTakeId"]) for item in expected):
                errors.append("latest assignments differ from the deterministic inventory")
    except (OSError, UnicodeError, ValueError, TypeError, RecursionError) as exc:
        errors.append(f"draft workspace evidence cannot be verified: {exc}")
    return ProductionResult(not errors, tuple(errors), ())
