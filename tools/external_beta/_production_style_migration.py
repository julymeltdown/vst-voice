"""Read-only migration preparation; durable application belongs to C++.

Only a validated, hash-matching legacy inventory with one declared style resolves
ownership. Neither a currently selected UI style nor a legacy range PASS is
migration or qualification evidence.
"""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path

from ._production_draft_validation import _project, read_bounded_object, validate_draft_workspace


def prepare_style_migration(workspace: Path, inventory: dict) -> dict:
    project, payload = read_bounded_object(workspace / "project.json")
    verified = validate_draft_workspace(workspace, inventory)
    if not verified.passed:
        raise ValueError("migration source cannot be verified: " + "; ".join(verified.errors))
    if read_bounded_object(workspace / "project.json")[1] != payload:
        raise ValueError("migration source changed during verification")
    if project["schemaVersion"] not in (1, 2, 3) or inventory.get("schemaVersion") != 1:
        raise ValueError("style migration requires an unchanged legacy producer and legacy inventory")
    result = {
        "format": "com.project-seam.production-style-migration-plan", "schemaVersion": 1,
        "sourceProjectId": project["projectId"], "sourceGeneration": project["lastDurableGeneration"],
        "sourceProjectSha256": hashlib.sha256(payload).hexdigest(),
        "inventorySha256": inventory["inventorySha256"],
        "capturedInventory": copy.deepcopy(inventory),
        "status": "UNRESOLVED", "unresolved": [], "releaseEligible": False,
        "qualification": "REASSESSMENT_REQUIRED",
    }
    styles = inventory.get("supportedStyles")
    if not isinstance(styles, list) or len(styles) != 1 or not isinstance(styles[0], str) or not styles[0]:
        result["unresolved"] = [{"coverageKey": row["coverageKey"], "pitchLayer": row["pitchLayer"],
            "reason": "Legacy inventory does not uniquely assign a style"} for row in project["unitAssignments"]]
        result["reason"] = "An explicit per-assignment evidence binding is required; no style was selected automatically"
        return result
    proposed = copy.deepcopy(project)
    proposed.update(schemaVersion=4, language=inventory["language"],
                    lifecycle="EXPERIMENTAL" if project["takes"] else "DRAFT")
    proposed.setdefault("sourceBindings", [])
    proposed.setdefault("sourceQualityAssessments", [])
    for take in proposed["takes"]:
        take["style"] = styles[0]
        take.setdefault("sourceBindingId", "")
    takes = {row["takeId"]: row for row in proposed["takes"]}
    for row in proposed["unitAssignments"]:
        row["style"] = styles[0]
        row["markerReviewed"] = row["pitchReviewed"] = False
        if row["state"] in ("APPROVED", "PITCH_REVIEW"):
            row["state"] = "MARKER_REVIEW"
            takes[row["takeId"]]["state"] = "MARKER_REVIEW"
    # The planner does not increment generation: only the durable writer may
    # choose and commit it. Old review/assessment records remain historical.
    errors = []
    _project(workspace, proposed, "proposed migration", errors)
    if errors:
        raise ValueError("proposed migration does not meet the target schema: " + "; ".join(errors))
    result.update(status="RESOLVED_NOT_APPLIED", proposedProject=proposed)
    return result


def encode_style_migration_plan(plan: dict) -> str:
    return json.dumps(plan, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n"
