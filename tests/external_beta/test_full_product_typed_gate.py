"""U45 typed full-product semantic audit: scenario, integration and mutation coverage.

The complete fixture is ENGINEERING_FIXTURE evidence under the reserved
synthetic contract.  Focused mutation tests run the typed layer directly;
end-to-end tests run the reader, JSON Schema and the real release gate.
"""
from __future__ import annotations

import copy
import hashlib
import itertools
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from tests.external_beta.full_product_typed_fixture import PRODUCER, complete_report, png, synthetic_beta_archive, tone_wav
from tools.external_beta import release_gate
from tools.external_beta.full_product_contract import CANONICAL_CONTRACT_ID, SYNTHETIC_CONTRACT_AUTHORITY_ERROR
from tools.external_beta.full_product_gate import (
    CANDIDATE_EVIDENCE,
    ENGINEERING_FIXTURE,
    FIXTURE_AUTHORITY_ERROR,
    _TypedAudit,
    approval_scope,
    audio_errors,
    audit_full_product_report,
    declared_incompatibilities,
    project_document_errors,
    released_resources,
    ui_capture_errors,
)
from tools.external_beta.release_audit import audit_release

ROOT = Path(__file__).resolve().parents[2]
CANONICAL = ROOT / "docs/product/full-product-beta-contract.json"


class _FixtureCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._directory = tempfile.TemporaryDirectory()
        cls.root = Path(cls._directory.name)
        cls.base_report, cls.base_contract = complete_report(cls.root)
        cls._names = itertools.count()

    @classmethod
    def tearDownClass(cls) -> None:
        cls._directory.cleanup()

    def setUp(self) -> None:
        self.report = copy.deepcopy(self.base_report)
        self.contract = copy.deepcopy(self.base_contract)

    def typed(self, candidate=None) -> list[str]:
        audit = _TypedAudit(self.report, self.contract, candidate, self.root.resolve())
        audit.run()
        return audit.errors

    def full(self, **arguments):
        return audit_full_product_report(self.report, full_product_contract=self.contract,
            report_path=self.root / "report.json", evidence_root=self.root, **arguments)

    def assertError(self, errors, fragment: str) -> None:
        self.assertTrue(any(fragment in error for error in errors), f"{fragment!r} not in {list(errors)[:12]}")

    def case(self, case_id: str) -> dict:
        return next(row for row in self.report["cases"] if row["id"] == case_id)

    def observation(self, case_id: str, index: int = 0) -> dict:
        return self.case(case_id)["observations"][index]

    def write(self, data) -> dict:
        name = f"mutations/{next(self._names)}.bin"
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = data if isinstance(data, bytes) else json.dumps(data, sort_keys=True).encode()
        path.write_bytes(payload)
        return {"locator": name, "sha256": hashlib.sha256(payload).hexdigest()}

    def rewrite(self, holder: dict, key: str, mutate) -> dict:
        """Rewrite the retained JSON behind holder[key] under a new name."""
        record = json.loads((self.root / holder[key]["locator"]).read_text())
        mutate(record)
        holder[key] = self.write(record)
        return record

    def artifact(self, observation: dict, kind: str) -> dict:
        return next(item for item in observation["artifacts"] if item["kind"] == kind)

    def replace_artifact(self, observation: dict, kind: str, data) -> dict:
        item = self.artifact(observation, kind)
        item.update(self.write(data))
        return item

    def refresh_reviews(self, observation: dict, case_id: str) -> None:
        """Re-issue the retained reviews for a deliberately changed observation."""
        material = sorted({item["sha256"] for item in observation["artifacts"] if item["kind"] != "independent-review"})
        observation["artifacts"] = [item for item in observation["artifacts"] if item["kind"] != "independent-review"]
        for review in observation["reviews"]:
            def mutate(record):
                record.update(approvalScope=approval_scope(observation, case_id), reviewedArtifacts=material)
            self.rewrite(review, "rawEvidence", mutate)
            observation["artifacts"].append({"kind": "independent-review", **review["rawEvidence"]})


class Scenario3SuccessPathTests(_FixtureCase):
    def test_component_fixture_passes_typed_checks_but_full_report_refuses_legacy_soaks(self) -> None:
        before = CANONICAL.read_bytes()
        result = self.full()
        self.assertEqual([], self.typed())
        from tests.external_beta.test_full_product_report import SOAK_ADMISSION_ERRORS
        self.assertEqual(SOAK_ADMISSION_ERRORS, result.errors)
        self.assertFalse(result.passed)
        self.assertEqual(("SYNTHETIC", ENGINEERING_FIXTURE), (result.contract_mode, result.evidence_class))
        self.assertFalse(result.authorizes_release)
        self.assertFalse(result.as_dict()["authorizesRelease"])
        self.assertEqual(83, len(self.report["cases"]))
        self.assertGreater(sum(len(row["observations"]) for row in self.report["cases"]), 83)
        self.assertEqual(before, CANONICAL.read_bytes())

    def test_fixture_cannot_be_laundered_through_the_canonical_identity_or_class(self) -> None:
        self.contract["contractId"] = CANONICAL_CONTRACT_ID
        self.assertError(self.typed(), "ENGINEERING_FIXTURE is not admitted by project-seam.full-product-beta")
        self.setUp()
        self.report["evidenceClass"] = CANDIDATE_EVIDENCE
        errors = self.typed()
        self.assertError(errors, "CANDIDATE_EVIDENCE is not admitted by synthetic-test-only.full-product-beta")
        self.assertError(errors, "evidenceClass differs from the report")

    def test_canonical_contract_blocks_the_same_report(self) -> None:
        result = audit_full_product_report(self.report, full_product_contract=json.loads(CANONICAL.read_text()),
            report_path=self.root / "report.json")
        self.assertFalse(result.passed)
        self.assertEqual("CANONICAL", result.contract_mode)
        for fragment in ("not frozen", "semanticValidation does not record", "declares no released resources",
                "is not admitted by project-seam.full-product-beta", "U45_RECONCILIATION_HOLD"):
            self.assertError(result.errors, fragment)


class Scenario1CoverageAdmissionTests(_FixtureCase):
    def test_missing_duplicate_unknown_and_deferred_cases_fail(self) -> None:
        # Missing and duplicate child cases are already outside the closed
        # evidence schema; unknown IDs fail its enum and deferred rows fail
        # the semantic reader.  All of them block the typed audit.
        mutations = {
            "missing": (lambda report: report["cases"].pop(), "schema cases"),
            "duplicate": (lambda report: report["cases"].append(copy.deepcopy(report["cases"][0])), "schema cases"),
            "unknown": (lambda report: report["cases"][0].update(id="R1.slider-screenshot"), "schema cases/0/id"),
            "deferred": (lambda report: report["cases"][3].update(status="NOT_RUN"), "R1.timing-tempo is not PASS"),
        }
        for name, (mutate, fragment) in mutations.items():
            with self.subTest(mutation=name):
                self.setUp()
                mutate(self.report)
                result = self.full()
                self.assertFalse(result.passed)
                self.assertError(result.errors, fragment)
                self.assertLess(max(len(error) for error in result.errors), 2000)

    def test_unfilled_acceptance_criteria_fail(self) -> None:
        for observation in self.case("R15.classical-small-edit")["observations"]:
            observation["measurements"] = [item for item in observation["measurements"] if item["criterionId"] != "classical-small-edit-p95"]
        self.assertError(self.typed(), "acceptance criteria are unfilled: classical-small-edit-p95")
        self.setUp()
        measurement = next(item for item in self.observation("R4.real-input")["measurements"] if item["criterionId"] == "producer-acceptance")
        self.rewrite(measurement, "rawEvidence", lambda record: record["constraintResults"]["handoff"].update(status="NOT_MET"))
        self.artifact(self.observation("R4.real-input"), "measurement")  # the retained artifact still names the old record
        errors = self.typed()
        self.assertError(errors, "protocol constraint handoff is not MET")
        self.assertError(errors, "measurement record is not retained as a measurement artifact")
        self.setUp()
        measurement = next(item for item in self.observation("R4.real-input")["measurements"] if item["criterionId"] == "producer-acceptance")
        self.rewrite(measurement, "rawEvidence", lambda record: record["constraintResults"].pop("handoff"))
        self.assertError(self.typed(), "every protocol constraint must be evaluated exactly once")

    def test_legacy_scope_without_a_neural_model_fails(self) -> None:
        sample = next(item for item in self.contract["scope"]["releasedResources"] if item["id"] == "fixture.sample-real")
        for observation in self.case("R9.installed-inference")["observations"]:
            observation["resourceIds"] = ["fixture.sample-real"]
            observation["bindings"] = copy.deepcopy(sample["bindings"])
        errors = self.typed()
        self.assertError(errors, "exercises no released resource of the case's resource kinds")
        self.assertError(errors, "R9.installed-inference: required resource kind neural-original has no observation")


class Scenario2FakeEvidenceTests(_FixtureCase):
    def test_fake_slider_screenshots_are_rejected(self) -> None:
        case_id = "R11.musical-editing"
        observation = self.observation(case_id)
        after = [item["sha256"] for item in observation["artifacts"] if item["kind"] == "project"][-1]
        log = self.artifact(observation, "session-log")
        record = json.loads((self.root / log["locator"]).read_text())
        record["operations"][0]["beforeProjectSha256"] = after
        log.update(self.write(record))
        self.refresh_reviews(observation, case_id)
        self.assertError(self.typed(), "a screenshot alone is not proof")
        self.setUp()
        observation = self.observation(case_id)
        record = json.loads((self.root / self.artifact(observation, "session-log")["locator"]).read_text())
        record["operations"][0].update(beforeProjectSha256=None, afterProjectSha256=None)
        self.artifact(observation, "session-log").update(self.write(record))
        self.refresh_reviews(observation, case_id)
        self.assertError(self.typed(), "binds captures without a project state change")
        for data, fragment in ((b"GIF89a-not-a-screenshot", "UI capture must be a PNG image"),
                (png()[:29] + b"\x00\x00\x00\x00" + png()[33:], "header checksum is invalid")):
            with self.subTest(fragment=fragment):
                self.setUp()
                observation = self.observation(case_id)
                self.replace_artifact(observation, "ui-capture", data)
                self.assertError(self.typed(), fragment)

    def test_wrong_resources_and_platforms_are_rejected(self) -> None:
        sample = next(item for item in self.contract["scope"]["releasedResources"] if item["id"] == "fixture.sample-real")
        observation = self.observation("R9.dataset-model")
        observation.update(resourceIds=["fixture.sample-real"], bindings=copy.deepcopy(sample["bindings"]))
        self.assertError(self.typed(), "exercises no released resource of the case's resource kinds")
        self.setUp()
        self.contract["scope"]["releasedResources"][0]["platforms"] = ["macos-arm64"]
        self.assertError(self.typed(), "must be released on every supported platform")
        self.setUp()
        for observation in self.case("R1.timing-tempo")["observations"]:
            observation.update(platform="macos-arm64", installedTreeSha256="3" * 64)
        self.assertError(self.typed(), "R1.timing-tempo: required platform windows-x86_64 has no observation")
        self.setUp()
        neural = next(item for item in self.contract["scope"]["releasedResources"] if item["id"] == "fixture.neural-original")
        neural["languages"] = ["ja"]
        self.assertError(self.typed(), "no exercised singer resource declares language en")
        candidate = {"evidence": [
            {"requirementId": "EB-009-full-product", "status": "PASS", "platform": "macos", "architecture": "arm64",
             "installedTreeSha256": "3" * 64, "finalDeliverableSha256": "2" * 64},
            {"requirementId": "EB-009-full-product", "status": "PASS", "platform": "windows", "architecture": "x86_64",
             "installedTreeSha256": "6" * 64, "finalDeliverableSha256": "2" * 64}]}
        self.setUp()
        self.assertEqual([], self.typed(candidate))
        windows = next(item for row in self.report["cases"] for item in row["observations"] if item["platform"] == "windows-x86_64")
        windows["installedTreeSha256"] = "3" * 64
        self.assertError(self.typed(candidate), "installed or signed bytes differ from the candidate's windows-x86_64 installation")
        self.setUp()
        self.assertError(self.typed({"evidence": candidate["evidence"][:1]}), "no EB-009 installed evidence for windows-x86_64")

    def test_stale_weights_recipes_and_packages_are_rejected(self) -> None:
        def binding(observation, kind):
            return next(item for item in observation["bindings"] if item["kind"] == kind)
        neural = next(item for item in self.case("R9.installed-inference")["observations"] if "fixture.neural-original" in item["resourceIds"])
        binding(neural, "model")["sha256"] = "e" * 64
        self.assertError(self.typed(), "stale or substituted model fixture.neural-original.content")
        self.setUp()
        recipe = next(item for item in self.case("R3.original-female-recipe")["observations"] if "fixture.recipe-original" in item["resourceIds"])
        binding(recipe, "recipe")["sha256"] = "e" * 64
        self.assertError(self.typed(), "stale or substituted recipe fixture.recipe-original.content")
        self.setUp()
        neural = next(item for item in self.case("R9.installed-inference")["observations"] if "fixture.neural-original" in item["resourceIds"])
        neural["bindings"] = [item for item in neural["bindings"] if item["kind"] != "vocoder"]
        neural["bindings"].append({"kind": "model", "id": "unreviewed-model", "version": "1", "sha256": "f" * 64})
        errors = self.typed()
        self.assertError(errors, "does not bind released vocoder")
        self.assertError(errors, "binds model unreviewed-model that no exercised released resource declares")
        self.setUp()
        observation = self.observation("R5.versioned-install")
        receipt = self.artifact(observation, "installed-resource")
        record = json.loads((self.root / receipt["locator"]).read_text())
        record["packageSha256"] = "e" * 64
        receipt.update(self.write(record))
        self.assertError(self.typed(), "installed package digest differs from the frozen resource matrix")

    def test_self_approved_and_unbound_musical_claims_are_rejected(self) -> None:
        case_id = "R1.cross-note-melody"
        review = self.observation(case_id)["reviews"][0]
        self.assertEqual("musician", review["role"])
        review["reviewerId"] = PRODUCER
        errors = self.typed()
        self.assertError(errors, "self-approved claim; the reviewer also produced candidate material")
        self.assertError(errors, "reviewer is not in the reviewer registry")
        self.setUp()
        self.rewrite(self.report, "reviewerRegistry", lambda record: record["reviewers"].append(
            {"reviewerId": PRODUCER, "kind": "HUMAN", "roles": ["musician"], "nativeLanguages": ["ja"]}))
        self.assertError(self.typed(), "cannot be both producer and reviewer")
        self.setUp()
        self.rewrite(self.report, "reviewerRegistry", lambda record: next(
            item for item in record["reviewers"] if item["reviewerId"] == "fixture-reviewer-musician").update(kind="GENERATOR"))
        self.assertError(self.typed(), "a generator identity cannot approve generated material")
        self.setUp()
        observation = self.observation(case_id)
        audio = {item["sha256"] for item in observation["artifacts"] if item["kind"] == "audio"}
        self.rewrite(observation["reviews"][0], "rawEvidence", lambda record: record.update(
            reviewedArtifacts=[digest for digest in record["reviewedArtifacts"] if digest not in audio]))
        self.assertError(self.typed(), "musical judgment does not cover every retained audio artifact")
        self.setUp()
        self.rewrite(self.observation(case_id)["reviews"][0], "rawEvidence",
            lambda record: record["approvalScope"].update(bindingsSha256="e" * 64))
        self.assertError(self.typed(), "approval scope differs from the exact resource, workload and revision observed")
        self.setUp()
        self.rewrite(self.report, "reviewerRegistry", lambda record: next(
            item for item in record["reviewers"] if item["reviewerId"] == "fixture-reviewer-native-language-reviewer").update(nativeLanguages=["ja"]))
        self.assertError(self.typed(), "native-language review requires a reviewer native in the observed language")
        self.setUp()
        self.observation(case_id)["reviews"][0]["rubricSha256"] = "e" * 64
        self.assertError(self.typed(), "rubric differs from the frozen independent-review protocol")

    def test_underlying_audio_projects_and_measurements_are_validated(self) -> None:
        case_id = "R1.cross-note-melody"
        silent = bytearray(tone_wav())
        silent[44:] = bytes(len(silent) - 44)
        for data, fragment in ((bytes(silent), "audio is digital silence"), (b"PASS", "audio must be a RIFF/WAVE file")):
            with self.subTest(fragment=fragment):
                self.setUp()
                self.replace_artifact(self.observation(case_id), "audio", data)
                self.assertError(self.typed(), fragment)
        self.setUp()
        self.replace_artifact(self.observation(case_id), "project", {"formatId": "slider-screenshot", "schemaVersion": 1})
        self.assertError(self.typed(), "must be a com.project-seam.project document")
        self.setUp()
        observation = self.observation(case_id)
        audio = {item["sha256"] for item in observation["artifacts"] if item["kind"] == "audio"}
        self.rewrite(observation["measurements"][0], "rawEvidence", lambda record: record.update(
            inputs=[digest for digest in record["inputs"] if digest not in audio]))
        self.assertError(self.typed(), "acoustic measurement does not measure any retained audio")
        self.setUp()
        neural = next(item for item in self.case("R9.installed-inference")["observations"] if item["platform"] == "macos-arm64")
        measurement = next(item for item in neural["measurements"] if item["criterionId"] == "neural-budgets")
        self.rewrite(measurement, "rawEvidence", lambda record: record.update(cellId=record["cellId"].replace("macos-arm64", "windows-x86_64")))
        self.assertError(self.typed(), "belongs to another platform")

    def test_derived_creator_claims_are_recomputed_from_retained_records(self) -> None:
        observation = self.observation("R16.independent-creators")
        measurement = next(item for item in observation["measurements"] if item["criterionId"] == "creator-count")
        measurement["value"] = 50
        self.assertError(self.typed(), "creator-count claims 50 but R16 retains only")
        self.setUp()
        study = self.observation("R10.manual-ownership")["creatorStudy"]
        study["participantId"] = PRODUCER
        self.rewrite(study, "rawEvidence", lambda record: record.update(participantId=PRODUCER))
        self.assertError(self.typed(), "is not a registered independent human participant")
        self.setUp()
        for row in self.report["cases"]:
            if row["id"].startswith("R10."):
                for item in row["observations"]:
                    item["creatorStudy"]["taskOrder"] = "MANUAL_THEN_ASSISTED"
                    self.rewrite(item["creatorStudy"], "rawEvidence", lambda record: record.update(taskOrder="MANUAL_THEN_ASSISTED"))
        self.assertError(self.typed(), "assisted comparison is not counterbalanced")

    def test_records_from_another_candidate_or_class_are_rejected(self) -> None:
        observation = self.observation("R2.vibrato")
        self.rewrite(observation["reviews"][0], "rawEvidence", lambda record: record.update(candidateRootId="older-candidate"))
        self.rewrite(observation["measurements"][0], "rawEvidence", lambda record: record.update(evidenceClass=CANDIDATE_EVIDENCE))
        errors = self.typed()
        self.assertError(errors, "candidateRootId differs from the report")
        self.assertError(errors, "evidenceClass differs from the report")

    def test_operations_must_transform_retained_inputs(self) -> None:
        check = self.observation("R2.vibrato")["checkResults"][0]
        operation = check["operationObservations"][0]
        operation["outputs"] = copy.deepcopy(operation["inputs"])
        self.assertError(self.typed(), "must show distinct retained inputs and outputs")


class MatrixDefinitionTests(unittest.TestCase):
    def setUp(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            from tests.external_beta.full_product_typed_fixture import synthetic_contract
            self.contract = synthetic_contract(Path(directory))

    def test_typed_resource_descriptors_are_enforced(self) -> None:
        resources, errors = released_resources(self.contract)
        self.assertEqual([], errors)
        self.assertEqual(6, len(resources))
        mutations = (
            (lambda entry: entry.update(packageKind="model"), "packageKind must be sample"),
            (lambda entry: entry.update(bindings=[{"kind": "recipe", "id": "x", "version": "1", "sha256": "a" * 64}]), "does not bind its primary bank"),
            (lambda entry: entry.pop("packageSha256"), "fields differ from the typed resource descriptor"),
            (lambda entry: entry.update(dependencies=[{"id": "fixture.neural-original", "version": "1", "sha256": "a" * 64}]), "differs from its released package digest"),
        )
        for mutate, fragment in mutations:
            with self.subTest(fragment=fragment):
                contract = copy.deepcopy(self.contract)
                mutate(contract["scope"]["releasedResources"][0])
                self.assertTrue(any(fragment in error for error in released_resources(contract)[1]), released_resources(contract)[1])
        contract = copy.deepcopy(self.contract)
        contract["scope"]["releasedResources"].append(copy.deepcopy(contract["scope"]["releasedResources"][0]))
        self.assertTrue(any("duplicate released resource" in error for error in released_resources(contract)[1]))

    def test_incompatibility_declarations_cannot_waive_platforms_or_whole_features(self) -> None:
        scope = self.contract["scope"]
        scope["declaredIncompatibilities"] = [{"caseId": "R1.cross-note-melody", "dimension": "backend", "value": "neural", "reason": "reviewed"}]
        declared, errors = declared_incompatibilities(self.contract)
        self.assertEqual([], errors)
        self.assertEqual({"R1.cross-note-melody": {"backend": {"neural"}}}, declared)
        for entry, fragment in (
            ({"caseId": "R1.cross-note-melody", "dimension": "platform", "value": "windows-x86_64", "reason": "x"}, "only a known case"),
            ({"caseId": "R1.cross-note-melody", "dimension": "backend", "value": "classical", "reason": "x"}, "cannot declare every backend unsupported"),
            ({"caseId": "R1.cross-note-melody", "dimension": "backend", "value": "procedural", "reason": "x"}, "is not a declared backend"),
            ({"caseId": "R1.cross-note-melody", "dimension": "language", "value": "ko", "reason": " "}, "a reviewed reason is required"),
        ):
            with self.subTest(fragment=fragment):
                scope["declaredIncompatibilities"] = [scope["declaredIncompatibilities"][0], entry]
                self.assertTrue(any(fragment in error for error in declared_incompatibilities(self.contract)[1]))


class ArtifactParserTests(unittest.TestCase):
    def test_audio_parser(self) -> None:
        self.assertEqual([], audio_errors(tone_wav(), "audio"))
        header = bytearray(tone_wav())
        self.assertTrue(audio_errors(bytes(header[:-3]), "audio"))
        float_audio = self._float_wav([0.25, -0.25])
        self.assertEqual([], audio_errors(float_audio, "audio"))
        self.assertTrue(any("non-finite" in error for error in audio_errors(self._float_wav([0.25, float("nan")]), "audio")))
        header[34:36] = struct.pack("<H", 12)
        self.assertTrue(any("integer PCM or IEEE float" in error for error in audio_errors(bytes(header), "audio")))

    @staticmethod
    def _float_wav(samples):
        data = struct.pack(f"<{len(samples)}f", *samples)
        fmt = struct.pack("<HHIIHH", 3, 1, 48000, 48000 * 4, 4, 32)
        body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
        return b"RIFF" + struct.pack("<I", len(body)) + body

    def test_png_and_project_parsers(self) -> None:
        self.assertEqual([], ui_capture_errors(png(), "capture"))
        self.assertTrue(ui_capture_errors(png()[:-4], "capture"))
        self.assertTrue(ui_capture_errors(png(0, 2), "capture"))
        document = {"formatId": "com.project-seam.project", "schemaVersion": 9, "projectId": "p", "ppq": 960, "tempoMap": [{"tick": 0, "bpm": 120}]}
        self.assertEqual([], project_document_errors(json.dumps(document).encode(), "project"))
        for key, value in (("ppq", 0), ("schemaVersion", True), ("tempoMap", [])):
            with self.subTest(key=key):
                self.assertTrue(project_document_errors(json.dumps(document | {key: value}).encode(), "project"))


class ReleaseGateIntegrationTests(unittest.TestCase):
    def test_complete_synthetic_candidate_is_audited_but_refused_ready_and_closed(self) -> None:
        from tests.production.public_release_replay_fixtures import LEGACY_SOAK_GATE_ERRORS
        expected = {SYNTHETIC_CONTRACT_AUTHORITY_ERROR, *(error.removeprefix("External Beta replay: gate: ") for error in LEGACY_SOAK_GATE_ERRORS)}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            built = synthetic_beta_archive(root)
            for state in ("EXTERNAL_BETA_READY", "EXTERNAL_BETA_CLOSED"):
                with self.subTest(state=state):
                    result = release_gate.evaluate_gate(built["candidate"], state, built["acceptance"],
                        archive_verified=True, evidence_root=root)
                    self.assertFalse(result.passed)
                    self.assertEqual(("EB-005-standalone-soak", "EB-009-full-product"), result.blocked_ids)
                    self.assertEqual(expected, set(result.errors))
            with mock.patch.dict(os.environ, {"SEAM_EXTERNAL_BETA_TRUSTED_ANCHOR_SHA256": built["trustedAnchor"]}):
                audit = audit_release(built["candidate"], built["manifest"], root, "CLOSED", acceptance_contract=built["acceptance"])
            self.assertFalse(audit.passed)
            self.assertEqual({"gate: " + error for error in expected}, set(audit.errors))
            (root / "synthetic-tone.wav").write_bytes(b"RIFF-substituted")
            result = release_gate.evaluate_ready(built["candidate"], built["acceptance"], archive_verified=True, evidence_root=root)
            self.assertTrue(any("content digest does not match" in error for error in result.errors), result.errors)

    def test_cli_reports_a_complete_fixture_as_non_authorizing(self) -> None:
        import subprocess
        import sys

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            synthetic_beta_archive(root)
            completed = subprocess.run([sys.executable, str(ROOT / "scripts/verify_full_product_report.py"),
                "--report", str(root / "report.json"), "--candidate", str(root / "candidate.json"),
                "--acceptance-contract", str(root / "acceptance.json"), "--full-product-contract", str(root / "full-contract.json")],
                cwd=ROOT, capture_output=True, text=True, check=False, timeout=120)
            payload = json.loads(completed.stdout)
            self.assertEqual(3, completed.returncode, completed.stdout[:2000] + completed.stderr[:2000])
            self.assertEqual(("BLOCKED", False, False, ENGINEERING_FIXTURE),
                (payload["status"], payload["passed"], payload["authorizesRelease"], payload["evidenceClass"]))


if __name__ == "__main__":
    unittest.main()
