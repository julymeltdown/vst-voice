"""The Python mirror of the C++ review transition: the same review basis bytes and the same rules.

Every take here is synthetic: hand-built rows, or the committed procedural "ma" bake imported through the C++ CLI.
A review decision in these tests is a fixture operator's recorded decision. No listening result, singer
qualification or release claim is made or implied.
"""
from __future__ import annotations

import copy
import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any, Callable

from tools.external_beta._production_review_transition import (
    REVIEW_MATERIAL_KIND, effective_audio, production_project_json, review_basis, review_transition_errors)
from tools.external_beta.voicebank_production import prepare_production_draft_definition, validate_production_draft_workspace
from tools.voicebank_script_generator.draft_inventory import generate_draft_inventory

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "build/release/seam_voicebank_cli"
BAKE = ROOT / "docs/implementation/evidence/nasal-consonant-2026-09-09/ma-bake"
CANDIDATE = BAKE / "candidates/00000000000153d9-00000000000153da"
RECIPE = BAKE / "recipes/7361e7c1d112ba26cf07284548c14866a865195f52d7737e4352b38382d8d2d2.json"
TAKE_SHA = "f67deb2dbe7dfdeba74988b94854973157e32f9d6bcec1b73384841dcc3eecff"
AT = "2026-09-28T10:00:00Z"


def _producer() -> dict[str, Any]:
    """Two active takes: take-a imported by the producer, take-i by a registered second reviewer."""
    strategy = {"id": "synthetic", "kind": "PROCEDURAL_SYNTHESIS", "rights": "PASS", "coverage": "NOT_ASSESSED",
                "listening": "NOT_ASSESSED", "permissions": {"sourceUse": True, "transformation": True,
                "singingBankRedistribution": False, "commercialRenders": False}, "licenseLocator": "/synthetic.txt",
                "licenseSha256": "1" * 64, "evidenceState": "SYNTHETIC_TEST_ONLY"}
    project: dict[str, Any] = {
        "format": "com.project-seam.voicebank-production-project", "schemaVersion": 2, "projectId": "mirror",
        "inventoryId": "fixture", "inventorySha256": "a" * 64, "selectedSourceStrategyId": "synthetic",
        "licenseLocator": "/synthetic.txt", "licenseSha256": "1" * 64, "immutableAssetRoot": "",
        "sourceStrategies": [strategy], "assets": [], "takes": [], "derivedRevisions": [], "metadataRevisions": [],
        "unitAssignments": [], "reviews": [], "lastDurableGeneration": 4, "lifecycle": "EXPERIMENTAL",
        "sourceBindings": [], "operators": [{"operatorId": "producer", "role": "PRODUCER"},
                                            {"operatorId": "reviewer", "role": "REVIEWER"},
                                            {"operatorId": "second-reviewer", "role": "REVIEWER"}]}
    for phone, digest, importer in (("a", "b" * 64, "producer"), ("i", "c" * 64, "second-reviewer")):
        take_id = f"take-{phone}"
        project["assets"].append({"sha256": digest, "relativePath": f"raw/{digest[:2]}/{digest}.wav",
                                  "byteSize": 100, "kind": "RAW"})
        project["takes"].append({"takeId": take_id, "promptId": phone, "coverageKey": f"sustain:{phone}",
                                 "pitchLayer": 69, "rawAssetSha256": digest, "derivedRevisionIds": [],
                                 "supersedesTakeId": "", "state": "MARKER_REVIEW", "sourceBindingId": f"source-{phone}"})
        project["sourceBindings"].append({"id": f"source-{phone}", "takeId": take_id, "rawAssetSha256": digest,
                                          "strategy": copy.deepcopy(strategy), "importerId": importer,
                                          "importedAtUtc": AT, "licenseSnapshotPath": f"source-evidence/{'1' * 64}.txt"})
        project["unitAssignments"].append({"coverageKey": f"sustain:{phone}", "pitchLayer": 69, "promptId": phone,
                                           "plannedTakeId": take_id, "takeId": take_id, "state": "MARKER_REVIEW",
                                           "markerReviewed": False, "pitchReviewed": False})
        # Stands in for the take-inspection.v2 receipt every import appends; its content is checked elsewhere.
        project["metadataRevisions"].append({"revisionId": f"take-inspection-{take_id}", "takeId": take_id,
            "rawAssetSha256": digest, "kind": "take-inspection.v2", "values": {"evidenceJson": "{}",
            "evidenceSha256": "0" * 64}, "operatorId": importer, "performedAtUtc": AT})
    return project


def _take(project: dict[str, Any], take_id: str) -> dict[str, Any]:
    return next(take for take in project["takes"] if take["takeId"] == take_id)


def _row(project: dict[str, Any], take_id: str) -> dict[str, Any]:
    return next(row for row in project["unitAssignments"] if row["takeId"] == take_id)


def _set_state(project: dict[str, Any], take_id: str, state: str) -> None:
    _take(project, take_id)["state"] = state
    _row(project, take_id).update(state=state, markerReviewed=state == "APPROVED", pitchReviewed=state == "APPROVED")


def _decide(previous: dict[str, Any], take_id: str, reviewer: str = "reviewer", result: str = "PASS",
            at: str = AT) -> dict[str, Any]:
    """The next generation as review-sample writes it: one decision, the material it judged, the take's state."""
    project = copy.deepcopy(previous)
    project["lastDurableGeneration"] += 1
    take = _take(project, take_id)
    review_id = f"review-{take_id}-{len(project['reviews'])}"
    project["reviews"].append({"reviewId": review_id, "takeId": take_id, "reviewerId": reviewer, "result": result,
                               "reviewedAtUtc": at})
    project["metadataRevisions"].append({
        "revisionId": "material-" + review_id, "takeId": take_id, "rawAssetSha256": take["rawAssetSha256"],
        "kind": REVIEW_MATERIAL_KIND, "operatorId": reviewer, "performedAtUtc": at,
        "values": {"unitId": "unit-" + take_id, "reviewId": review_id, "audioSha256": effective_audio(project, take),
                   "unitManifestSha256": "d" * 64, "reviewBasisSha256": review_basis(project, take_id)}})
    _set_state(project, take_id, "APPROVED" if result == "PASS" else "REJECTED")
    return project


def _annotate(previous: dict[str, Any], take_id: str, operator: str = "producer") -> dict[str, Any]:
    project = copy.deepcopy(previous)
    project["lastDurableGeneration"] += 1
    project["metadataRevisions"].append({
        "revisionId": f"markers-{take_id}-{len(project['metadataRevisions'])}", "takeId": take_id,
        "rawAssetSha256": _take(project, take_id)["rawAssetSha256"], "kind": "MARKERS_AND_PITCH",
        "values": {"vowelOnset": "120"}, "operatorId": operator, "performedAtUtc": AT})
    return project


def _with(project: dict[str, Any], mutate: Callable[[dict[str, Any]], Any]) -> dict[str, Any]:
    changed = copy.deepcopy(project)
    mutate(changed)
    return changed


class ReviewTransitionRuleTest(unittest.TestCase):
    def assertRefused(self, errors: list[str], message: str) -> None:
        self.assertTrue(any(message in error for error in errors), errors)

    def test_only_an_independent_decision_on_current_material_grants_review_status(self) -> None:
        base = _producer()
        decided = _decide(base, "take-a")
        self.assertEqual([], review_transition_errors(base, decided, "review", "g5"))
        rejected = _decide(base, "take-a", result="REJECTED")
        self.assertEqual([], review_transition_errors(base, rejected, "review", "g5"))
        for action in ("save", "marker", "transform", "retake", "select-take", "import"):
            self.assertRefused(review_transition_errors(base, decided, action, "g5"), "outside an independent review")
        hand = _with(base, lambda project: _set_state(project, "take-a", "APPROVED"))
        self.assertRefused(review_transition_errors(base, hand, "save", "g5"), "grants review status to take-a")
        self.assertRefused(review_transition_errors(base, hand, "review", "g5"), "with the material it was made on")
        flagged = _with(base, lambda project: _row(project, "take-a").update(markerReviewed=True))
        self.assertRefused(review_transition_errors(base, flagged, "save", "g5"), "grants review status to take-a")
        bare = _with(hand, lambda project: project["reviews"].append(
            {"reviewId": "bare", "takeId": "take-a", "reviewerId": "reviewer", "result": "PASS", "reviewedAtUtc": AT}))
        self.assertRefused(review_transition_errors(base, bare, "review", "g5"), "with the material it was made on")
        self.assertRefused(review_transition_errors(base, bare, "save", "g5"), "outside an independent review")

        def material(project: dict[str, Any]) -> dict[str, Any]:
            return project["metadataRevisions"][-1]

        forgeries: tuple[tuple[str, dict[str, Any], str], ...] = (
            ("unregistered reviewer", _decide(base, "take-a", reviewer="producer"), "needs a registered reviewer"),
            ("importer reviews", _decide(base, "take-i", reviewer="second-reviewer"), "importer"),
            ("stale basis", _with(decided, lambda p: material(p)["values"].update(reviewBasisSha256="0" * 64)),
             "does not describe its current audio and review basis"),
            ("other audio", _with(decided, lambda p: material(p)["values"].update(audioSha256="c" * 64)),
             "does not describe its current audio and review basis"),
            ("material by another operator", _with(decided, lambda p: material(p).update(operatorId="second-reviewer")),
             "does not describe its current audio and review basis"),
            ("material at another time", _with(decided, lambda p: material(p).update(performedAtUtc="2026-09-28T11:00:00Z")),
             "does not describe its current audio and review basis"),
            ("extra material value", _with(decided, lambda p: material(p)["values"].update(listening="PASS")),
             "does not describe its current audio and review basis"),
            ("other decision named", _with(decided, lambda p: material(p)["values"].update(reviewId="other")),
             "does not describe its current audio and review basis"),
            ("legacy material kind", _with(decided, lambda p: material(p).update(kind="sample-candidate-review-v1")),
             "not current or not recorded once"),
            ("decision without material", _with(decided, lambda p: p["metadataRevisions"].pop()),
             "with the material it was made on"),
            ("material without decision", _with(decided, lambda p: p["reviews"].pop()),
             "with the material it was made on"),
            ("rejection that approves", _with(decided, lambda p: p["reviews"][-1].update(result="REJECTED")),
             "does not set exactly its own active take's state"),
            ("grant beyond the decision", _with(decided, lambda p: _set_state(p, "take-i", "APPROVED")),
             "grants review status to take-i without an independent PASS decision"),
            ("decided twice", _with(decided, lambda p: p["reviews"].append(dict(p["reviews"][-1], reviewId="again"))),
             "decides a take more than once"),
        )
        for name, forged, message in forgeries:
            with self.subTest(name):
                self.assertRefused(review_transition_errors(base, forged, "review", "g5"), message)

    def test_whoever_processed_or_annotated_a_take_cannot_review_it(self) -> None:
        processed = _producer()
        processed["assets"].append({"sha256": "e" * 64, "relativePath": f"derived/ee/{'e' * 64}.wav",
                                    "byteSize": 100, "kind": "DERIVED"})
        processed["derivedRevisions"].append({"revisionId": "a-gain", "inputSha256": "b" * 64, "outputSha256": "e" * 64,
            "operation": "NORMALIZE_GAIN", "operationVersion": "1", "parameters": {"targetPeak": "0.15"},
            "operatorId": "reviewer", "performedAtUtc": AT})
        _take(processed, "take-a")["derivedRevisionIds"] = ["a-gain"]
        self.assertRefused(review_transition_errors(processed, _decide(processed, "take-a"), "review", "g"),
                           "processed the take's audio")
        self.assertEqual([], review_transition_errors(processed, _decide(processed, "take-a", "second-reviewer"),
                                                      "review", "g"))
        self.assertEqual("e" * 64, _decide(processed, "take-a", "second-reviewer")["metadataRevisions"][-1]["values"]["audioSha256"])
        annotated = _annotate(_producer(), "take-a", "reviewer")
        self.assertRefused(review_transition_errors(annotated, _decide(annotated, "take-a"), "review", "g"),
                           "annotated or inspected")

    def test_an_approval_needs_the_current_take_inspection_receipt(self) -> None:
        legacy = _with(_producer(), lambda p: p.update(metadataRevisions=[
            row for row in p["metadataRevisions"] if row["kind"] != "take-inspection.v2"]))
        self.assertRefused(review_transition_errors(legacy, _decide(legacy, "take-a"), "review", "g"),
                           "without its current take-inspection.v2 receipt")
        self.assertEqual([], review_transition_errors(legacy, _decide(legacy, "take-a", result="REJECTED"), "review", "g"))
        stale = _with(_producer(), lambda p: p["metadataRevisions"][0].update(rawAssetSha256="c" * 64))
        self.assertRefused(review_transition_errors(stale, _decide(stale, "take-a"), "review", "g"),
                           "without its current take-inspection.v2 receipt")
        self.assertEqual([], review_transition_errors(_producer(), _decide(_producer(), "take-a"), "review", "g"))

    def test_history_and_manual_work_are_append_only(self) -> None:
        decided = _decide(_annotate(_producer(), "take-a"), "take-a")
        edits: tuple[tuple[str, Callable[[dict[str, Any]], Any], str], ...] = (
            ("removed decision", lambda p: p["reviews"].pop(), "append-only review decisions"),
            ("rewritten decision", lambda p: p["reviews"][0].update(result="REJECTED"), "append-only review decisions"),
            ("removed material", lambda p: p["metadataRevisions"].pop(), "annotations, receipts and review material"),
            ("rewritten annotation", lambda p: p["metadataRevisions"][0]["values"].update(vowelOnset="90"),
             "annotations, receipts and review material"),
            ("removed audio", lambda p: p["assets"].pop(), "stored take audio"),
            ("removed take", lambda p: p["takes"].pop(), "removes take take-i"),
            ("renamed prompt", lambda p: _take(p, "take-a").update(promptId="renamed"), "identity, lineage"),
            ("replaced audio", lambda p: _take(p, "take-a").update(rawAssetSha256="c" * 64), "identity, lineage"),
            ("rebound source", lambda p: _take(p, "take-a").update(sourceBindingId="source-i"), "identity, lineage"),
            ("new lineage", lambda p: _take(p, "take-a").update(supersedesTakeId="take-i"), "identity, lineage"),
        )
        for name, edit, message in edits:
            with self.subTest(name):
                self.assertRefused(review_transition_errors(decided, _with(decided, edit), "save", "g"), message)
        processed = _with(decided, lambda p: (
            p["derivedRevisions"].append({"revisionId": "a-gain", "inputSha256": "b" * 64, "outputSha256": "e" * 64,
                "operation": "NORMALIZE_GAIN", "operationVersion": "1", "parameters": {}, "operatorId": "producer",
                "performedAtUtc": AT}),
            _take(p, "take-a")["derivedRevisionIds"].append("a-gain")))
        self.assertRefused(review_transition_errors(processed, _with(processed, lambda p: p["derivedRevisions"].pop()),
                                                    "save", "g"), "processing history")
        self.assertRefused(review_transition_errors(processed, _with(processed, lambda p: _take(p, "take-a").update(
            derivedRevisionIds=[])), "save", "g"), "identity, lineage or processing chain")

    def test_changed_material_must_lower_exactly_the_approvals_it_invalidates(self) -> None:
        approved = _decide(_decide(_producer(), "take-a"), "take-i")
        annotated = _annotate(approved, "take-a")
        self.assertRefused(review_transition_errors(approved, annotated, "marker", "g"), "keeps review status for take-a")
        lowered = _with(annotated, lambda p: _set_state(p, "take-a", "MARKER_REVIEW"))
        self.assertEqual([], review_transition_errors(approved, lowered, "marker", "g"))
        self.assertEqual([], review_transition_errors(lowered, _decide(lowered, "take-a"), "review", "g"))
        replanned = _with(approved, lambda p: p.update(inventorySha256="f" * 64))
        errors = review_transition_errors(approved, replanned, "save", "g")
        self.assertRefused(errors, "keeps review status for take-a")
        self.assertRefused(errors, "keeps review status for take-i")
        both = _with(replanned, lambda p: (_set_state(p, "take-a", "MARKER_REVIEW"), _set_state(p, "take-i", "MARKER_REVIEW")))
        self.assertEqual([], review_transition_errors(approved, both, "save", "g"))
        # A decision recorded before an edit does not describe the edited take.
        for kept in ("take-a", "take-i"):
            self.assertNotEqual(review_basis(approved, kept), review_basis(replanned, kept))
        self.assertEqual(review_basis(approved, "take-i"), review_basis(annotated, "take-i"))

    def test_a_legacy_approval_stands_until_its_own_material_changes(self) -> None:
        legacy = _producer()
        _set_state(legacy, "take-a", "APPROVED")
        legacy["reviews"].append({"reviewId": "legacy", "takeId": "take-a", "reviewerId": "reviewer", "result": "PASS",
                                  "reviewedAtUtc": AT})
        self.assertEqual([], review_transition_errors(legacy, _annotate(legacy, "take-i"), "marker", "g"))
        self.assertRefused(review_transition_errors(legacy, _annotate(legacy, "take-a"), "marker", "g"),
                           "keeps review status for take-a")

    def test_a_selected_alternative_starts_unreviewed(self) -> None:
        approved = _decide(_producer(), "take-a")
        retaken = copy.deepcopy(approved)
        retaken["takes"].append(dict(_take(approved, "take-a"), takeId="take-a2", supersedesTakeId="take-a",
                                     sourceBindingId="source-a2", state="MARKER_REVIEW"))
        retaken["sourceBindings"].append(dict(retaken["sourceBindings"][0], id="source-a2", takeId="take-a2"))
        _take(retaken, "take-a")["state"] = "RETAKE"
        _row(retaken, "take-a").update(takeId="take-a2", state="MARKER_REVIEW", markerReviewed=False, pitchReviewed=False)
        self.assertEqual([], review_transition_errors(approved, retaken, "retake", "g"))

        def select(project: dict[str, Any], state: str) -> None:
            _take(project, "take-a2")["state"] = "RETAKE"
            _take(project, "take-a")["state"] = state
            _row(project, "take-a2").update(takeId="take-a", state=state, markerReviewed=state == "APPROVED",
                                            pitchReviewed=state == "APPROVED")

        self.assertEqual([], review_transition_errors(retaken, _with(retaken, lambda p: select(p, "MARKER_REVIEW")),
                                                      "select-take", "g"))
        self.assertRefused(review_transition_errors(retaken, _with(retaken, lambda p: select(p, "APPROVED")),
                                                    "select-take", "g"), "grants review status to take-a")

    def test_a_new_producer_and_an_empty_assignment_are_unreviewed(self) -> None:
        fresh = _producer()
        self.assertEqual([], review_transition_errors(None, fresh, "create", "g1"))
        self.assertRefused(review_transition_errors(None, _decide(fresh, "take-a"), "create", "g1"), "starts a new producer")
        empty = _with(fresh, lambda p: p["unitAssignments"].append({"coverageKey": "sustain:u", "pitchLayer": 69,
            "promptId": "u", "plannedTakeId": "take-u", "takeId": "", "state": "MISSING", "markerReviewed": True,
            "pitchReviewed": False}))
        self.assertRefused(review_transition_errors(fresh, empty, "save", "g"), "empty assignment")
        self.assertRefused(review_transition_errors(None, empty, "create", "g1"), "empty assignment")

    def test_unverifiable_generations_are_refused_rather_than_skipped(self) -> None:
        broken = _with(_producer(), lambda p: p.pop("reviews"))
        self.assertRefused(review_transition_errors(_producer(), broken, "save", "g"), "cannot be verified")


@unittest.skipUnless(CLI.is_file(), "Build the actual production CLI for cross-language parity")
class CppReviewTransitionParityTest(unittest.TestCase):
    """Decisions the C++ CLI records bind the mirrored basis; forged generations are refused for their own cause."""

    def _ok(self, *arguments: str) -> dict[str, Any]:
        done = subprocess.run([str(CLI), *arguments], capture_output=True, text=True, timeout=60)
        self.assertEqual(0, done.returncode, done.stderr)
        return json.loads(done.stdout)

    def _producer(self, root: Path) -> tuple[Path, dict[str, Any], dict[int, dict[str, Any]]]:
        inventory = generate_draft_inventory({"profileId": "review-transition-parity", "vowels": ["a"],
            "consonants": ["m"], "includeKinds": ["cv"], "alternateTakes": 1, "pitchLayers": [60, 69],
            "requestedRange": {"minMidi": 60, "maxMidi": 72}})
        draft = prepare_production_draft_definition(inventory, None, project_id="review-transition-parity",
                                                    operator_id="producer")
        draft["operators"].append({"operatorId": "reviewer", "role": "REVIEWER"})
        definition = root / "definition.json"
        definition.write_bytes((json.dumps(draft, sort_keys=True, indent=2) + "\n").encode())
        workspace = root / "workspace"
        created = self._ok("init-production", str(workspace), str(definition),
                           hashlib.sha256(definition.read_bytes()).hexdigest(), "producer", "2026-09-28T00:00:00Z")
        license_path = root / "license.txt"
        license_path.write_text("SYNTHETIC TEST ONLY: procedural fixture, no singer qualification")
        self._ok("register-source", str(workspace), created["projectSha256"], "procedural-a", "procedural", "pass",
                 "yes", "yes", "no", "no", str(license_path), hashlib.sha256(license_path.read_bytes()).hexdigest(),
                 "producer", "2026-09-28T00:01:00Z")
        units = {row["pitchLayer"]: row for row in self._project(workspace)["unitAssignments"]}
        for minute, pitch in enumerate((69, 60), start=2):
            row = units[pitch]
            self._ok("import-procedural", str(workspace), str(CANDIDATE) + ".json", str(CANDIDATE) + ".wav",
                     str(RECIPE), row["plannedTakeId"], row["promptId"], row["coverageKey"], str(pitch), "producer",
                     f"2026-09-28T00:{minute:02}:00Z")
        return workspace, inventory, units

    @staticmethod
    def _project(workspace: Path) -> dict[str, Any]:
        return json.loads((workspace / "project.json").read_bytes())

    @staticmethod
    def _append(source: Path, target: Path, mutate: Callable[[dict[str, Any]], Any], action: str, subject: str,
                actor: str = "producer") -> Path:
        """A copy with one more generation, written in the exact C++ encoding, that the CLI did not write."""
        shutil.copytree(source, target)
        project = json.loads((source / "project.json").read_bytes())
        project["lastDurableGeneration"] += 1
        mutate(project)
        payload = production_project_json(project).encode()
        name = f"{project['lastDurableGeneration']:020}.json"
        journal = {"format": "com.project-seam.voicebank-production-journal-event", "schemaVersion": 1,
                   "generation": project["lastDurableGeneration"], "projectSha256": hashlib.sha256(payload).hexdigest(),
                   "action": action, "subjectId": subject, "operatorId": actor, "occurredAtUtc": "2026-09-28T01:00:00Z"}
        (target / "generations" / name).write_bytes(payload)
        (target / "journal" / name).write_bytes((json.dumps(journal, sort_keys=True, indent=2) + "\n").encode())
        (target / "project.json").write_bytes(payload)
        return target

    def test_cli_decisions_bind_the_mirrored_basis_through_retake_and_selection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace, inventory, units = self._producer(root)
            self._ok("create-sample-draft", str(workspace), "review.parity", "0.1.0", "Review parity fixture", "ja",
                     "neutral", str(root / "editable"))
            manifest = json.loads((root / "editable/manifest.json").read_text())
            unit = next(row["id"] for row in manifest["units"] if row["rootMidi"] == 69)
            captured = self._ok("prepare-sample-review", str(workspace), str(root / "editable/manifest.json"),
                                str(root / "packet.json"))
            self._ok("review-sample", str(workspace), str(root / "packet.json"), captured["fileSha256"], "reviewer",
                     "2026-09-28T00:05:00Z", "accept", unit)

            # Every generation the CLI wrote re-encodes byte for byte, and the recorded basis is the mirrored basis.
            for path in sorted((workspace / "generations").glob("*.json")):
                self.assertEqual(path.read_bytes().decode(), production_project_json(json.loads(path.read_bytes())))
            project = self._project(workspace)
            approved, waiting = units[69]["plannedTakeId"], units[60]["plannedTakeId"]
            material = [row for row in project["metadataRevisions"] if row["kind"] == REVIEW_MATERIAL_KIND]
            self.assertEqual([approved], [row["takeId"] for row in material])
            self.assertEqual(review_basis(project, approved), material[0]["values"]["reviewBasisSha256"])
            self.assertEqual(TAKE_SHA, material[0]["values"]["audioSha256"])
            self.assertEqual("APPROVED", _take(project, approved)["state"])
            verified = validate_production_draft_workspace(workspace, inventory)
            self.assertTrue(verified.passed, verified.errors)
            reviewed = root / "reviewed"
            shutil.copytree(workspace, reviewed)

            # Forged next generations, each refused for its own cause; the control is what C++ would write.
            def decision(reviewer: str, basis: str | None = None) -> Callable[[dict[str, Any]], None]:
                def forge(value: dict[str, Any]) -> None:
                    take = _take(value, waiting)
                    value["reviews"].append({"reviewId": "forged", "takeId": waiting, "reviewerId": reviewer,
                                             "result": "PASS", "reviewedAtUtc": "2026-09-28T01:00:00Z"})
                    value["metadataRevisions"].append({"revisionId": "forged-material", "takeId": waiting,
                        "rawAssetSha256": take["rawAssetSha256"], "kind": REVIEW_MATERIAL_KIND, "operatorId": reviewer,
                        "performedAtUtc": "2026-09-28T01:00:00Z", "values": {"unitId": "forged-unit",
                            "reviewId": "forged", "audioSha256": effective_audio(value, take), "unitManifestSha256": "d" * 64,
                            "reviewBasisSha256": basis or review_basis(value, waiting)}})
                    _set_state(value, waiting, "APPROVED")
                return forge

            def annotation(lower: bool) -> Callable[[dict[str, Any]], None]:
                def forge(value: dict[str, Any]) -> None:
                    value["metadataRevisions"].append({"revisionId": "forged-markers", "takeId": approved,
                        "rawAssetSha256": _take(value, approved)["rawAssetSha256"], "kind": "MARKERS_AND_PITCH",
                        "values": {"vowelOnset": "120"}, "operatorId": "producer", "performedAtUtc": "2026-09-28T01:00:00Z"})
                    if lower:
                        _set_state(value, approved, "MARKER_REVIEW")
                return forge

            controls = (("independent decision", decision("reviewer"), "review", "reviewer"),
                        ("annotation that lowers the approval", annotation(True), "marker", "producer"))
            for index, (name, forge, action, actor) in enumerate(controls):
                with self.subTest(name):
                    target = self._append(reviewed, root / f"control-{index}", forge, action, waiting, actor)
                    result = validate_production_draft_workspace(target, inventory)
                    self.assertTrue(result.passed, result.errors)
            forgeries = (
                ("hand approval", lambda value: _set_state(value, waiting, "APPROVED"), "save", "producer",
                 f"grants review status to {waiting}"),
                ("decision on another basis", decision("reviewer", "0" * 64), "review", "reviewer",
                 "does not describe its current audio and review basis"),
                ("producer decides", decision("producer"), "review", "producer", "needs a registered reviewer"),
                ("rewritten decision", lambda value: value["reviews"][0].update(result="REJECTED"), "save", "producer",
                 "append-only review decisions"),
                ("stale approval kept", annotation(False), "marker", "producer", f"keeps review status for {approved}"),
            )
            for index, (name, forge, action, actor, message) in enumerate(forgeries):
                with self.subTest(name):
                    target = self._append(reviewed, root / f"forged-{index}", forge, action, waiting, actor)
                    result = validate_production_draft_workspace(target, inventory)
                    self.assertFalse(result.passed)
                    self.assertTrue(any(message in error for error in result.errors), result.errors)

            # A retake of the approved take, then the original selected back: history kept, review required again.
            retake = approved[:-4] + "-t02"
            self._ok("import-procedural", str(workspace), str(CANDIDATE) + ".json", str(CANDIDATE) + ".wav", str(RECIPE),
                     retake, units[69]["promptId"], units[69]["coverageKey"], "69", "producer", "2026-09-28T00:06:00Z",
                     approved)
            self.assertEqual("RETAKE", _take(self._project(workspace), approved)["state"])
            current = hashlib.sha256((workspace / "project.json").read_bytes()).hexdigest()
            selected = self._ok("select-take", str(workspace), current, approved, "producer", "2026-09-28T00:07:00Z")
            self.assertEqual(("TakeSelected", "MARKER_REVIEW"), (selected["result"], selected["state"]))
            project = self._project(workspace)
            self.assertEqual(("MARKER_REVIEW", "RETAKE"), (_take(project, approved)["state"], _take(project, retake)["state"]))
            self.assertEqual((approved, False), (_row(project, approved)["takeId"], _row(project, approved)["markerReviewed"]))
            self.assertEqual(1, len(project["reviews"]))
            journal = json.loads((workspace / "journal" / f"{project['lastDurableGeneration']:020}.json").read_bytes())
            self.assertEqual(("select-take", approved), (journal["action"], journal["subjectId"]))
            for path in sorted((workspace / "generations").glob("*.json")):
                self.assertEqual(path.read_bytes().decode(), production_project_json(json.loads(path.read_bytes())))
            verified = validate_production_draft_workspace(workspace, inventory)
            self.assertTrue(verified.passed, verified.errors)
            refused = subprocess.run([str(CLI), "select-take", str(workspace), current, approved, "producer",
                                      "2026-09-28T00:08:00Z"], capture_output=True, text=True, timeout=60)
            self.assertNotEqual(0, refused.returncode)
            self.assertEqual(project, self._project(workspace))
            # Restoring the earlier approval without a new review is a forged generation.
            restored = self._append(workspace, root / "restored", lambda value: _set_state(value, approved, "APPROVED"),
                                    "select-take", approved)
            result = validate_production_draft_workspace(restored, inventory)
            self.assertFalse(result.passed)
            self.assertTrue(any(f"grants review status to {approved}" in error for error in result.errors), result.errors)


if __name__ == "__main__":
    unittest.main()
