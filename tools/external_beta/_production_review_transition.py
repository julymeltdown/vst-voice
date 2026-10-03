"""The C++ review transition, mirrored for the external producer-history validator.

libs/seam-voicebank-production/src/review_transition.cpp enforces these rules on the single durable append path;
review_transition_errors applies the same rules to each pair of generations in a workspace's history, so a
hand-edited generation that grants review status, rewrites history or keeps a stale approval is refused here too.
review_basis reproduces reviewBasis byte for byte (the C++ project encoding, the same filtering), which is what a
recorded decision binds. A decision passing these checks is traceable to an independent reviewer; it is not a
listening result, singer qualification or release claim.
"""
from __future__ import annotations

import copy
import hashlib
import json
from typing import Any

REVIEW_MATERIAL_KINDS = frozenset({"sample-candidate-review-v1", "sample-candidate-review-v2"})
REVIEW_MATERIAL_KIND = "sample-candidate-review-v2"
REVIEW_MATERIAL_VALUES = frozenset({"unitId", "reviewId", "audioSha256", "unitManifestSha256", "reviewBasisSha256"})
REVIEW_BASIS_PREFIX = "sample-candidate-review-basis-v2\n"
TAKE_INSPECTION_KIND = "take-inspection.v2"  # kTakeInspectionRevisionKind; _production_draft_validation checks its content
_LEVELS = {"APPROVED": 2, "PITCH_REVIEW": 1}
_PRODUCER_FIELDS = ("schemaVersion", "projectId", "inventoryId", "inventorySha256", "selectedSourceStrategyId",
                    "licenseLocator", "licenseSha256", "immutableAssetRoot", "language", "sourceStrategies",
                    "sourceQualityAssessments")
_TAKE_MATERIAL = ("takeId", "promptId", "coverageKey", "pitchLayer", "rawAssetSha256", "derivedRevisionIds",
                  "supersedesTakeId", "sourceBindingId", "style")
_TAKE_IDENTITY = ("promptId", "coverageKey", "pitchLayer", "rawAssetSha256", "supersedesTakeId", "sourceBindingId")


def production_project_json(project: dict[str, Any]) -> str:
    """encodeProductionProject for a decoded generation: stringifyJson(pretty) plus its trailing newline."""
    return json.dumps(project, sort_keys=True, indent=2, ensure_ascii=False) + "\n\n"


def review_basis(project: dict[str, Any], take_id: str) -> str:
    """reviewBasis(project, takeId): the digest of everything a decision on this take judged, review status aside."""
    basis = copy.deepcopy(project)
    schema = basis["schemaVersion"]
    basis["lastDurableGeneration"] = 0
    basis["reviews"], basis["operators"] = [], []
    if schema >= 2:
        basis["lifecycle"] = "EXPERIMENTAL"
    basis["metadataRevisions"] = [row for row in basis["metadataRevisions"] if row["kind"] not in REVIEW_MATERIAL_KINDS
                                  and (not take_id or row["takeId"] == take_id)]
    if take_id:
        basis["takes"] = [take for take in basis["takes"] if take["takeId"] == take_id]
        basis["unitAssignments"] = [row for row in basis["unitAssignments"] if row["takeId"] == take_id]
        revisions = {identity for take in basis["takes"] for identity in take["derivedRevisionIds"]}
        assets = {take["rawAssetSha256"] for take in basis["takes"]}
        basis["derivedRevisions"] = [row for row in basis["derivedRevisions"] if row["revisionId"] in revisions]
        for row in basis["derivedRevisions"]:
            assets.update((row["inputSha256"], row["outputSha256"]))
        basis["assets"] = [row for row in basis["assets"] if row["sha256"] in assets]
        bindings = {take["sourceBindingId"] for take in basis["takes"] if take.get("sourceBindingId")}
        if "sourceBindings" in basis:
            basis["sourceBindings"] = [row for row in basis["sourceBindings"] if row["id"] in bindings]
        strategies = {row["strategy"]["id"] for row in basis.get("sourceBindings", [])}
        if schema >= 2 and strategies:
            basis.update(selectedSourceStrategyId="", licenseLocator="", licenseSha256="")
            basis["sourceStrategies"] = [row for row in basis["sourceStrategies"] if row["id"] in strategies]
            if "sourceQualityAssessments" in basis:
                basis["sourceQualityAssessments"] = [row for row in basis["sourceQualityAssessments"]
                                                     if row["strategyId"] in strategies]
        else:
            basis["sourceStrategies"] = [row for row in basis["sourceStrategies"]
                                         if row["id"] == basis["selectedSourceStrategyId"]]
    for take in basis["takes"]:
        take["state"] = "MARKER_REVIEW"
    for row in basis["unitAssignments"]:
        row.update(state="MARKER_REVIEW" if row["takeId"] else "MISSING", markerReviewed=False, pitchReviewed=False)
    # Only the declared layers this take's own assignment uses, so the basis does not move when an
    # unrelated assignment is added. reviewBasis in C++ applies the same narrowing; the two digests
    # have to agree or a decision the library accepts would be refused by the producer.
    if take_id and "declaredPitchLayers" in basis:
        used = {row["pitchLayer"] for row in basis["unitAssignments"]}
        basis["declaredPitchLayers"] = [layer for layer in basis["declaredPitchLayers"] if layer in used]
    return hashlib.sha256((REVIEW_BASIS_PREFIX + production_project_json(basis)).encode()).hexdigest()


def effective_audio(project: dict[str, Any], take: dict[str, Any]) -> str:
    """The audio a review of this take hears: its raw asset through its processing chain; empty if broken."""
    derived = {row["revisionId"]: row for row in project["derivedRevisions"]}
    audio = take["rawAssetSha256"]
    for identity in take["derivedRevisionIds"]:
        row = derived.get(identity)
        if row is None or row["inputSha256"] != audio:
            return ""
        audio = row["outputSha256"]
    return audio


def _levels(project: dict[str, Any]) -> dict[str, int]:
    levels = {take["takeId"]: _LEVELS.get(take["state"], 0) for take in project["takes"]}
    for row in project["unitAssignments"]:
        if row["takeId"]:
            flags = 1 if row["markerReviewed"] or row["pitchReviewed"] else 0
            levels[row["takeId"]] = max(levels.get(row["takeId"], 0), _LEVELS.get(row["state"], 0), flags)
    return levels


def _approval_applies(project: dict[str, Any], take: dict[str, Any]) -> bool:
    take_id = take["takeId"]
    review = next((row for row in reversed(project["reviews"]) if row["takeId"] == take_id), None)
    material = next((row for row in reversed(project["metadataRevisions"])
                     if row["takeId"] == take_id and row["kind"] in REVIEW_MATERIAL_KINDS), None)
    if review is None or material is None or review["result"] != "PASS" or material["kind"] != REVIEW_MATERIAL_KIND:
        return False
    values = material["values"]
    return (values.get("reviewId") == review["reviewId"] and values.get("audioSha256") == effective_audio(project, take)
            and values.get("reviewBasisSha256") == review_basis(project, take_id))


def _independence_error(project: dict[str, Any], take: dict[str, Any], reviewer: str) -> str | None:
    if not any(row["operatorId"] == reviewer and row["role"] == "REVIEWER" for row in project["operators"]):
        return "needs a registered reviewer"
    binding = next((row for row in project.get("sourceBindings", []) if row["id"] == take.get("sourceBindingId")), None)
    if take.get("sourceBindingId") and binding is not None and binding["importerId"] == reviewer:
        return "is made by the take's importer"
    chain = set(take["derivedRevisionIds"])
    if any(row["operatorId"] == reviewer and row["revisionId"] in chain for row in project["derivedRevisions"]):
        return "is made by whoever processed the take's audio"
    if any(row["takeId"] == take["takeId"] and row["kind"] not in REVIEW_MATERIAL_KINDS and row["operatorId"] == reviewer
           for row in project["metadataRevisions"]):
        return "is made by whoever annotated or inspected the take"
    return None


def _history_errors(previous: dict[str, Any], project: dict[str, Any], action: str) -> list[str]:
    errors = []
    for field, what in (("reviews", "review decisions"), ("metadataRevisions", "annotations, receipts and review material"),
                        ("derivedRevisions", "processing history"), ("assets", "stored take audio")):
        before, after = previous[field], project[field]
        if len(after) < len(before) or after[:len(before)] != before:
            errors.append(f"rewrites or removes append-only {what}")
    takes = {take["takeId"]: take for take in project["takes"]}
    for take in previous["takes"]:
        kept = takes.get(take["takeId"])
        if kept is None:
            errors.append(f"removes take {take['takeId']} and its manual work")
            continue
        chain = take["derivedRevisionIds"]
        if (any(kept.get(field) != take.get(field) for field in _TAKE_IDENTITY) or
                (action != "style-migration" and kept.get("style") != take.get("style")) or
                kept["derivedRevisionIds"][:len(chain)] != chain):
            errors.append(f"rewrites take {take['takeId']} identity, lineage or processing chain")
    return errors


def _decision_errors(previous: dict[str, Any], project: dict[str, Any], granted: set[str]) -> list[str]:
    decisions: dict[str, dict[str, Any]] = {}
    for review in project["reviews"][len(previous["reviews"]):]:
        if review["takeId"] in decisions:
            return ["review decides a take more than once"]
        decisions[review["takeId"]] = review
    recorded: dict[str, dict[str, Any]] = {}
    for row in project["metadataRevisions"][len(previous["metadataRevisions"]):]:
        if row["kind"] not in REVIEW_MATERIAL_KINDS:
            continue
        if row["kind"] != REVIEW_MATERIAL_KIND or row["takeId"] in recorded:
            return ["review material is not current or not recorded once per decided take"]
        recorded[row["takeId"]] = row
    if not decisions or len(recorded) != len(decisions):
        return ["review event does not record each decision with the material it was made on"]
    takes = {take["takeId"]: take for take in project["takes"]}
    errors = []
    for take_id, review in decisions.items():
        take, material = takes.get(take_id), recorded.get(take_id)
        if take is None or material is None or not review["reviewId"] or review["result"] not in ("PASS", "REJECTED"):
            errors.append(f"review decision for {take_id} lacks its take, material or a PASS/REJECTED result")
            continue
        independence = _independence_error(project, take, review["reviewerId"])
        if independence:
            errors.append(f"review decision for {take_id} {independence}")
            continue
        passed = review["result"] == "PASS"
        if passed and not any(row["takeId"] == take_id and row["kind"] == TAKE_INSPECTION_KIND and
                              row["rawAssetSha256"] == take["rawAssetSha256"] for row in project["metadataRevisions"]):
            errors.append(f"review decision for {take_id} approves a take without its current take-inspection.v2 receipt")
            continue
        values = material["values"]
        if (set(values) != REVIEW_MATERIAL_VALUES or values["reviewId"] != review["reviewId"] or
                material["operatorId"] != review["reviewerId"] or material["performedAtUtc"] != review["reviewedAtUtc"] or
                material["rawAssetSha256"] != take["rawAssetSha256"] or values["audioSha256"] != effective_audio(project, take) or
                values["reviewBasisSha256"] != review_basis(project, take_id)):
            errors.append(f"review material for {take_id} does not describe its current audio and review basis")
            continue
        decided = "APPROVED" if passed else "REJECTED"
        assignment = next((row for row in project["unitAssignments"] if row["takeId"] == take_id), None)
        if (assignment is None or take["state"] != decided or assignment["state"] != decided or
                assignment["markerReviewed"] != passed or assignment["pitchReviewed"] != passed):
            errors.append(f"review decision for {take_id} does not set exactly its own active take's state")
    for take_id in sorted(granted):
        if decisions.get(take_id, {}).get("result") != "PASS":
            errors.append(f"grants review status to {take_id} without an independent PASS decision")
    return errors


def _affected(previous: dict[str, Any], project: dict[str, Any]) -> set[str] | None:
    """The takes whose review basis may differ between two generations; None means every take."""
    if (any(previous.get(field) != project.get(field) for field in _PRODUCER_FIELDS) or
            ((previous["schemaVersion"] < 2 or project["schemaVersion"] < 2) and
             previous.get("lifecycle") != project.get("lifecycle"))):
        return None
    before = {take["takeId"]: take for take in previous["takes"]}
    affected = {take["takeId"] for take in project["takes"]
                if take["takeId"] not in before or any(before[take["takeId"]].get(field) != take.get(field)
                                                       for field in _TAKE_MATERIAL)}

    def rows(value: dict[str, Any]) -> dict[str, list[tuple]]:
        grouped: dict[str, list[tuple]] = {}
        for row in value["unitAssignments"]:
            if row["takeId"]:
                grouped.setdefault(row["takeId"], []).append((row["coverageKey"], row["pitchLayer"], row["promptId"],
                                                              row["plannedTakeId"], row.get("style")))
        return grouped

    was, now = rows(previous), rows(project)
    affected.update(take_id for take_id in was.keys() | now.keys() if was.get(take_id) != now.get(take_id))
    affected.update(row["takeId"] for row in project["metadataRevisions"][len(previous["metadataRevisions"]):]
                    if row["kind"] not in REVIEW_MATERIAL_KINDS)
    return affected


def review_transition_errors(previous: dict[str, Any] | None, project: dict[str, Any], action: str,
                             label: str) -> list[str]:
    """applyReviewTransition from previous (None for a new producer) to project, as the journal action wrote it.

    C++ lowers a stale approval before it commits, so a generation it wrote never carries one; a stale approval
    here means the generation was not written through the durable path.
    """
    try:
        errors = []
        if any(not row["takeId"] and (row["markerReviewed"] or row["pitchReviewed"] or _LEVELS.get(row["state"], 0))
               for row in project["unitAssignments"]):
            errors.append("gives review status to an empty assignment")
        after = _levels(project)
        if previous is None:
            if (project["reviews"] or any(row["kind"] in REVIEW_MATERIAL_KINDS for row in project["metadataRevisions"]) or
                    any(after.values())):
                errors.append("starts a new producer with review status or decisions")
            return [f"{label} {error}" for error in errors]
        errors.extend(_history_errors(previous, project, action))
        if errors:
            return [f"{label} {error}" for error in errors]
        before = _levels(previous)
        granted = {take_id for take_id, level in after.items() if level > before.get(take_id, 0)}
        appended = (len(project["reviews"]) != len(previous["reviews"]) or
                    any(row["kind"] in REVIEW_MATERIAL_KINDS
                        for row in project["metadataRevisions"][len(previous["metadataRevisions"]):]))
        if action == "review":
            errors.extend(_decision_errors(previous, project, granted))
        elif appended:
            errors.append("records review decisions or material outside an independent review")
        elif granted:
            errors.append(f"grants review status to {sorted(granted)[0]} without an independent review")
        affected = _affected(previous, project)
        takes = {take["takeId"]: take for take in project["takes"]}
        for take_id, level in sorted(after.items()):
            if level == 0 or take_id in granted or (affected is not None and take_id not in affected):
                continue
            if take_id not in takes or not _approval_applies(project, takes[take_id]):
                errors.append(f"keeps review status for {take_id} after the material it was decided on changed")
        return [f"{label} {error}" for error in errors]
    except (KeyError, TypeError, AttributeError, IndexError):
        return [f"{label} review transition cannot be verified"]
