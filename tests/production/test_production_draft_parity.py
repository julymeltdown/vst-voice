from __future__ import annotations

import copy
import hashlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

from tools.external_beta.voicebank_production import (
    prepare_production_draft_definition,
    validate_production_draft_workspace,
    validate_source_execution,
    validate_source_strategy_document,
    validate_source_strategy_draft,
)

ROOT = Path(__file__).resolve().parents[2]
from tools.external_beta._production_draft_validation import _quality_policy_identity, _quality_material_identity, _quality_current


def _text(value: dict) -> bytes:
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()


def _source(root: Path, identity: str = "source-a") -> dict:
    payload = f"GENERATED TEST SOURCE EVIDENCE: {identity}".encode()
    (root / f"{identity}.txt").write_bytes(payload)
    return {"id": identity, "kind": "PROCEDURAL_SYNTHESIS", "rights": "PASS",
            "coverage": "NOT_ASSESSED", "listening": "NOT_ASSESSED",
            "permissions": {"sourceUse": True, "transformation": True,
                            "singingBankRedistribution": False, "commercialRenders": False},
            "licenseLocator": f"{identity}.txt", "licenseSha256": hashlib.sha256(payload).hexdigest(),
            "evidenceState": "SYNTHETIC_TEST_ONLY"}


def _document(source: dict | None = None) -> dict:
    return {"schemaVersion": 2, "status": "DRAFT", "assetAdmissionStatus": "NOT_RUN",
            "selectedStrategyId": source["id"] if source else "", "strategies": [source] if source else []}


def _ancestry(workspace: Path, parent: int, aborted: tuple[int, ...] = ()) -> dict:
    return {"format": "parent-and-aborted-journals-v1", "parentGeneration": parent,
            "parentProjectSha256": hashlib.sha256((workspace / "generations" / f"{parent:020}.json").read_bytes()).hexdigest() if parent else "",
            "parentJournalSha256": hashlib.sha256((workspace / "journal" / f"{parent:020}.json").read_bytes()).hexdigest() if parent else "",
            "abortedGenerations": [{"generation": number,
                                    "journalSha256": hashlib.sha256((workspace / "journal" / f"{number:020}.json").read_bytes()).hexdigest(),
                                    "journalBytes": (workspace / "journal" / f"{number:020}.json").stat().st_size} for number in aborted]}


def _generation(workspace: Path, project: dict, number: int, action: str, subject: str, actor: str = "producer",
                ancestry: dict | None = None) -> None:
    project["lastDurableGeneration"] = number
    payload = _text(project)
    journal = {"format": "com.project-seam.voicebank-production-journal-event", "schemaVersion": 1,
               "generation": number, "projectSha256": hashlib.sha256(payload).hexdigest(), "action": action,
               "subjectId": subject, "operatorId": actor, "occurredAtUtc": "2026-09-09T10:00:00Z"}
    if ancestry is not None:
        journal["ancestry"] = ancestry
    (workspace / "generations" / f"{number:020}.json").write_bytes(payload)
    (workspace / "journal" / f"{number:020}.json").write_bytes(_text(journal))
    (workspace / "project.json").write_bytes(payload)


def _workspace(root: Path, recovered: bool = False) -> tuple[Path, dict]:
    """Durable C++ schema fixture; production creation stays in init-production."""
    workspace = root / "workspace"
    for name in ("assets", "staging", "generations", "journal", "source-evidence"):
        (workspace / name).mkdir(parents=True)
    project = prepare_production_draft_definition(None, None, project_id="draft-parity", operator_id="producer")
    project["operators"].append({"operatorId": "other-producer", "role": "PRODUCER"})
    _generation(workspace, project, 1, "create", "draft-parity")
    first = _source(root)
    first["licenseLocator"] = str(root / first["licenseLocator"])
    project.update({"inventoryId": "test-inventory", "inventorySha256": "a" * 64,
                    "selectedSourceStrategyId": first["id"], "sourceStrategies": [first],
                    "licenseLocator": first["licenseLocator"], "licenseSha256": first["licenseSha256"]})
    project["unitAssignments"] = [
        {"coverageKey": f"sustain:{phone}", "pitchLayer": 69, "promptId": f"prompt-{phone}",
         "plannedTakeId": f"take-{phone}", "takeId": "", "state": "MISSING", "markerReviewed": False, "pitchReviewed": False}
        for phone in ("a", "i")
    ]
    _generation(workspace, project, 2, "save", "draft-parity")
    stream = io.BytesIO()
    with wave.open(stream, "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(48000)
        output.writeframes(b"\x01\x00" * 32)
    wav = stream.getvalue()
    digest = hashlib.sha256(wav).hexdigest()
    relative = f"raw/{digest[:2]}/{digest}.wav"
    (workspace / "assets" / relative).parent.mkdir(parents=True)
    (workspace / "assets" / relative).write_bytes(wav)
    project["assets"] = [{"sha256": digest, "relativePath": relative, "byteSize": len(wav), "kind": "RAW"}]

    def capture(phone: str, strategy: dict, actor: str) -> None:
        identity = f"source-take-{phone}"
        project["takes"].append({"takeId": f"take-{phone}", "promptId": f"prompt-{phone}", "coverageKey": f"sustain:{phone}",
                                 "pitchLayer": 69, "rawAssetSha256": digest, "derivedRevisionIds": [], "supersedesTakeId": "",
                                 "state": "MARKER_REVIEW", "sourceBindingId": identity})
        snapshot = f"source-evidence/{strategy['licenseSha256']}.txt"
        (workspace / snapshot).write_bytes(Path(strategy["licenseLocator"]).read_bytes())
        project["sourceBindings"].append({"id": identity, "takeId": f"take-{phone}", "rawAssetSha256": digest,
                                          "strategy": copy.deepcopy(strategy), "importerId": actor,
                                          "importedAtUtc": "2026-09-09T10:00:00Z", "licenseSnapshotPath": snapshot})
        row = next(item for item in project["unitAssignments"] if item["plannedTakeId"] == f"take-{phone}")
        row.update({"takeId": f"take-{phone}", "state": "MARKER_REVIEW"})
        project["lifecycle"] = "EXPERIMENTAL"

    capture("a", first, "producer")
    _generation(workspace, project, 3, "import", "take-a")
    second = _source(root, "source-b")
    second["licenseLocator"] = str(root / second["licenseLocator"])
    project["sourceStrategies"].append(second)
    project.update({"selectedSourceStrategyId": second["id"], "licenseLocator": second["licenseLocator"], "licenseSha256": second["licenseSha256"]})
    if recovered:
        (workspace / "journal/00000000000000000004.json").write_bytes(b"interrupted")
        _generation(workspace, project, 5, "save", "source-b", ancestry=_ancestry(workspace, 3, (4,)))
    else:
        _generation(workspace, project, 4, "save", "source-b")
    capture("i", second, "other-producer")
    _generation(workspace, project, 6 if recovered else 5, "import", "take-i", "other-producer",
                _ancestry(workspace, 5) if recovered else None)
    return workspace, project


class ProductionDraftParityTests(unittest.TestCase):
    def test_actual_cli_registers_source_without_prefilled_policy_or_approval(self) -> None:
        cli = ROOT / "build/release/seam_voicebank_cli"
        if not cli.is_file():
            self.skipTest("Build the actual production CLI for cross-language parity")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            draft = prepare_production_draft_definition(None, None, project_id="registration", operator_id="producer")
            definition = root / "draft.json"
            definition.write_bytes(_text(draft))
            workspace = root / "workspace"
            initialized = subprocess.run([str(cli), "init-production", str(workspace), str(definition),
                hashlib.sha256(definition.read_bytes()).hexdigest(), "producer", "2026-09-09T10:00:00Z"],
                capture_output=True, text=True, timeout=20)
            self.assertEqual(0, initialized.returncode, initialized.stderr)
            source = _source(root)
            args = [str(cli), "register-source", str(workspace), json.loads(initialized.stdout)["projectSha256"],
                source["id"], "procedural", "pass", "yes", "yes", "no", "no", str(root/source["licenseLocator"]),
                source["licenseSha256"], "producer", "2026-09-09T10:01:00Z"]
            before = (workspace / "project.json").read_bytes()
            invalid = args.copy()
            invalid[7] = "maybe"
            self.assertNotEqual(0, subprocess.run(invalid, capture_output=True, timeout=20).returncode)
            self.assertEqual(before, (workspace / "project.json").read_bytes())
            registered = subprocess.run(args, capture_output=True, text=True, timeout=20)
            self.assertEqual(0, registered.returncode, registered.stderr)
            receipt = json.loads(registered.stdout)
            self.assertEqual("SourceRegistered", receipt["result"])
            self.assertFalse(receipt["releaseEligible"])
            project = json.loads((workspace / "project.json").read_bytes())
            self.assertEqual("NOT_ASSESSED", project["sourceStrategies"][0]["coverage"])
            self.assertFalse(project["sourceStrategies"][0]["permissions"]["singingBankRedistribution"])
            self.assertEqual([], project["takes"])
            result = validate_production_draft_workspace(workspace)
            self.assertTrue(result.passed, result.errors)
            unchanged = (workspace / "project.json").read_bytes()
            self.assertNotEqual(0, subprocess.run(args, capture_output=True, timeout=20).returncode)
            self.assertEqual(unchanged, (workspace / "project.json").read_bytes())

    def test_actual_cli_records_schema3_quality_with_matching_python_identities(self) -> None:
        cli = ROOT / "build/release/seam_voicebank_cli"
        if not cli.is_file():
            self.skipTest("Build the actual production CLI for cross-language parity")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace, project = _workspace(root)
            project["operators"].append({"operatorId":"reviewer","role":"REVIEWER"})
            _generation(workspace,project,6,"save","register-reviewer")
            inspection = subprocess.run([str(cli),"inspect-source-quality",str(workspace),"source-a"],capture_output=True,text=True,timeout=20)
            self.assertEqual(0,inspection.returncode,inspection.stderr)
            captured = json.loads(inspection.stdout)
            self.assertFalse(captured["qualityAssessmentRecorded"])
            self.assertEqual(_quality_policy_identity(project["sourceStrategies"][0]),captured["policySha256"])
            self.assertEqual(_quality_material_identity(project,"source-a"),captured["materialSha256"])
            evidence = root/"review.txt"; evidence.write_text("Synthetic cross-language reviewer fixture, not real voice acceptance.")
            arguments = [str(cli),"record-source-quality",str(workspace),"source-a",captured["projectSha256"],"quality-1","reviewer",
                         "2026-09-09T10:01:00Z","pass","pass",str(evidence),hashlib.sha256(evidence.read_bytes()).hexdigest()]
            saved = subprocess.run(arguments,capture_output=True,text=True,timeout=20)
            self.assertEqual(0,saved.returncode,saved.stderr)
            inspected_again = subprocess.run([str(cli),"inspect-source-quality",str(workspace),"source-a"],capture_output=True,text=True,timeout=20)
            self.assertEqual(0,inspected_again.returncode,inspected_again.stderr)
            self.assertTrue(json.loads(inspected_again.stdout)["recordedQualityPassingForCurrentMaterial"])
            result = validate_production_draft_workspace(workspace)
            self.assertTrue(result.passed,result.errors)
            current = json.loads((workspace/"project.json").read_bytes())
            self.assertEqual(3,current["schemaVersion"])
            self.assertTrue(_quality_current(current,"source-a"))
            self.assertFalse(current["sourceStrategies"][0]["permissions"]["singingBankRedistribution"])
            self.assertEqual(project["sourceBindings"],current["sourceBindings"])
            duplicate = subprocess.run(arguments,capture_output=True,text=True,timeout=20)
            self.assertNotEqual(0,duplicate.returncode)
            changed = copy.deepcopy(current); changed["inventorySha256"] = "b"*64
            self.assertFalse(_quality_current(changed,"source-a"))
            retained = workspace/"source-evidence"/(current["sourceQualityAssessments"][0]["evidenceSha256"]+".quality.txt")
            retained.write_text("tampered")
            self.assertFalse(validate_production_draft_workspace(workspace).passed)

    def test_schema3_quality_history_cannot_be_erased_or_relabelled_as_an_ordinary_save(self) -> None:
        for mutation in ("erase","wrong-action","self-review"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                root=Path(directory); workspace,project=_workspace(root)
                project["operators"].append({"operatorId":"reviewer","role":"REVIEWER"})
                _generation(workspace,project,6,"save","register-reviewer")
                evidence=b"Synthetic assessment evidence"
                digest=hashlib.sha256(evidence).hexdigest()
                (workspace/"source-evidence"/(digest+".quality.txt")).write_bytes(evidence)
                row={"id":"quality-1","strategyId":"source-a","policySha256":_quality_policy_identity(project["sourceStrategies"][0]),
                     "materialSha256":_quality_material_identity(project,"source-a"),"evidenceSha256":digest,"reviewerId":"reviewer",
                     "reviewedAtUtc":"2026-09-09T10:00:00Z","coverage":"PASS","listening":"PASS"}
                project.update({"schemaVersion":3,"sourceQualityAssessments":[row]})
                project["sourceStrategies"][0].update({"coverage":"PASS","listening":"PASS"})
                if mutation=="self-review":
                    row["reviewerId"]="producer"
                _generation(workspace,project,7,"save" if mutation=="wrong-action" else "source-quality-assessment",row["id"],row["reviewerId"])
                if mutation=="erase":
                    self.assertTrue(validate_production_draft_workspace(workspace).passed)
                    project["sourceQualityAssessments"]=[]
                    _generation(workspace,project,8,"save","erase-history")
                self.assertFalse(validate_production_draft_workspace(workspace).passed)

    def test_recovery_protocol_does_not_relax_legacy_source_qualification(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / "legacy"
            for name in ("assets", "staging", "generations", "journal"):
                (workspace / name).mkdir(parents=True)
            source = _source(root)
            source["licenseLocator"] = str(root / source["licenseLocator"])
            source["coverage"] = source["listening"] = "PASS"
            source["permissions"] = {name: True for name in source["permissions"]}
            project = prepare_production_draft_definition(None, None, project_id="legacy", operator_id="producer")
            project.pop("lifecycle")
            project.pop("sourceBindings")
            project.update({"schemaVersion": 1, "inventoryId": "synthetic", "inventorySha256": "a" * 64,
                            "sourceStrategies": [source], "selectedSourceStrategyId": source["id"],
                            "licenseLocator": source["licenseLocator"], "licenseSha256": source["licenseSha256"]})
            _generation(workspace, project, 1, "create", "legacy")
            result = validate_production_draft_workspace(workspace)
            self.assertTrue(result.passed, result.errors)
            original = (workspace / "generations/00000000000000000001.json").read_bytes()
            (workspace / "journal/00000000000000000002.json").write_bytes(b"interrupted")
            source["listening"] = "NOT_ASSESSED"
            _generation(workspace, project, 3, "save", "legacy", ancestry=_ancestry(workspace, 1, (2,)))
            result = validate_production_draft_workspace(workspace)
            self.assertFalse(result.passed)
            self.assertTrue(any("legacy producer qualification" in error for error in result.errors), result.errors)
            self.assertEqual(original, (workspace / "generations/00000000000000000001.json").read_bytes())

    def test_certified_recovery_retains_aborted_bytes_and_distinct_original_importers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, project = _workspace(Path(directory), recovered=True)
            original = {path: path.read_bytes() for folder in ("generations", "journal") for path in (workspace / folder).iterdir()}
            result = validate_production_draft_workspace(workspace)
            self.assertTrue(result.passed, result.errors)
            self.assertFalse((workspace / "generations/00000000000000000004.json").exists())
            self.assertEqual(b"interrupted", (workspace / "journal/00000000000000000004.json").read_bytes())
            self.assertEqual(["producer", "other-producer"], [item["importerId"] for item in project["sourceBindings"]])
            self.assertEqual([], project["reviews"])
            self.assertEqual(original, {path: path.read_bytes() for path in original})

    def test_recovery_rejects_missing_or_tampered_history_without_reclassifying_it(self) -> None:
        for mutation in ("missing_abort", "changed_abort", "missing_commit", "missing_certificate", "changed_parent",
                         "boolean_parent", "boolean_abort", "oversized_abort", "unknown_abort_field", "snapshot_for_abort", "uncertified_tail"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                workspace, _ = _workspace(Path(directory), recovered=True)
                certificate_path = workspace / "journal/00000000000000000005.json"
                journal = json.loads(certificate_path.read_bytes())
                if mutation == "missing_abort":
                    (workspace / "journal/00000000000000000004.json").unlink()
                elif mutation == "changed_abort":
                    (workspace / "journal/00000000000000000004.json").write_bytes(b"altered")
                elif mutation == "missing_commit":
                    (workspace / "generations/00000000000000000003.json").unlink()
                    (workspace / "journal/00000000000000000003.json").unlink()
                elif mutation == "missing_certificate":
                    del journal["ancestry"]
                elif mutation == "changed_parent":
                    journal["ancestry"]["parentProjectSha256"] = "a" * 64
                elif mutation == "boolean_parent":
                    journal["ancestry"]["parentGeneration"] = True
                elif mutation == "boolean_abort":
                    journal["ancestry"]["abortedGenerations"][0]["generation"] = True
                elif mutation == "oversized_abort":
                    journal["ancestry"]["abortedGenerations"][0]["journalBytes"] = 1024 * 1024 + 1
                elif mutation == "unknown_abort_field":
                    journal["ancestry"]["abortedGenerations"][0]["approved"] = True
                elif mutation == "snapshot_for_abort":
                    (workspace / "generations/00000000000000000004.json").write_bytes(b"fake snapshot")
                else:
                    (workspace / "journal/00000000000000000007.json").write_bytes(b"interrupted but not certified")
                certificate_path.write_bytes(_text(journal))
                result = validate_production_draft_workspace(workspace)
                self.assertFalse(result.passed, result.errors)

    def test_abort_evidence_does_not_follow_symlinks_or_accept_duplicate_fields(self) -> None:
        for mutation in ("symlink", "duplicate"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                workspace, _ = _workspace(root, recovered=True)
                if mutation == "symlink":
                    aborted = workspace / "journal/00000000000000000004.json"
                    outside = root / "outside-abort.txt"
                    outside.write_bytes(aborted.read_bytes())
                    aborted.unlink()
                    aborted.symlink_to(outside)
                else:
                    journal = workspace / "journal/00000000000000000005.json"
                    text = journal.read_text()
                    journal.write_text(text.replace('"parentGeneration": 3', '"parentGeneration": 3, "parentGeneration": 3', 1))
                self.assertFalse(validate_production_draft_workspace(workspace).passed)

    def test_source_free_and_unassessed_definitions_do_not_assert_qualification(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            definition = prepare_production_draft_definition(None, None, project_id="empty", operator_id="producer")
            self.assertEqual((2, "DRAFT", 0), (definition["schemaVersion"], definition["lifecycle"], definition["lastDurableGeneration"]))
            self.assertEqual([], definition["sourceStrategies"])
            self.assertEqual([], definition["sourceBindings"])
            self.assertEqual("", definition["licenseSha256"])
            pending = _source(root)
            pending.update({"rights": "NOT_ASSESSED", "licenseLocator": "planned.txt", "licenseSha256": ""})
            pending["permissions"] = {key: False for key in pending["permissions"]}
            before = copy.deepcopy(pending)
            document = _document(pending)
            self.assertTrue(validate_source_strategy_draft(document, root).passed)
            self.assertFalse(validate_source_execution(document, root).passed)
            self.assertFalse(validate_source_strategy_document(document, root).passed)
            definition = prepare_production_draft_definition(None, document, project_id="pending", operator_id="producer", repository_root=root)
            self.assertEqual("NOT_ASSESSED", definition["sourceStrategies"][0]["listening"])
            self.assertEqual("", definition["sourceStrategies"][0]["licenseSha256"])
            self.assertEqual(before, pending)

    def test_execution_requires_only_applicable_permissions_and_real_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = _document(_source(root))
            self.assertTrue(validate_source_strategy_draft(document, root).passed)
            self.assertTrue(validate_source_execution(document, root).passed)
            self.assertFalse(validate_source_strategy_document(document, root).passed)
            for field in ("sourceUse", "transformation"):
                changed = copy.deepcopy(document)
                changed["strategies"][0]["permissions"][field] = False
                self.assertTrue(validate_source_strategy_draft(changed, root).passed)
                self.assertFalse(validate_source_execution(changed, root).passed)
            (root / "source-a.txt").write_text("changed")
            self.assertTrue(validate_source_strategy_draft(document, root).passed)
            self.assertFalse(validate_source_execution(document, root).passed)

    def test_schema2_verifies_shared_bytes_and_retained_source_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace, project = _workspace(root)
            initial = {path: path.read_bytes() for path in (workspace / "generations").iterdir()}
            (root / "source-a.txt").unlink()
            (root / "source-b.txt").unlink()
            result = validate_production_draft_workspace(workspace)
            self.assertTrue(result.passed, result.errors)
            self.assertEqual(1, len(project["assets"]))
            self.assertEqual(2, len(project["sourceBindings"]))
            self.assertNotEqual(project["sourceBindings"][0]["importerId"], project["sourceBindings"][1]["importerId"])
            self.assertEqual(initial, {path: path.read_bytes() for path in initial})
            retained = workspace / project["sourceBindings"][0]["licenseSnapshotPath"]
            retained.write_text("changed retained source")
            result = validate_production_draft_workspace(workspace)
            self.assertFalse(result.passed)
            self.assertTrue(any("retained bytes" in error for error in result.errors), result.errors)

    def test_schema2_cannot_rewrite_sources_or_borrow_shared_blob_ownership(self) -> None:
        for mutation in ("snapshot", "owner", "actor", "qualified", "boolean"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                workspace, project = _workspace(Path(directory))
                if mutation == "snapshot":
                    project["sourceBindings"][0]["strategy"]["permissions"]["commercialRenders"] = True
                elif mutation == "owner":
                    project["takes"][1]["sourceBindingId"] = project["takes"][0]["sourceBindingId"]
                elif mutation == "actor":
                    project["sourceBindings"][1]["importerId"] = "producer"
                elif mutation == "qualified":
                    project["lifecycle"] = "QUALIFIED"
                else:
                    project["sourceBindings"][1]["strategy"]["permissions"]["sourceUse"] = 1
                _generation(workspace, project, 5, "import", "take-i", "other-producer")
                self.assertFalse(validate_production_draft_workspace(workspace).passed)

    def test_missing_history_and_duplicate_json_fields_fail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, _ = _workspace(Path(directory))
            (workspace / "generations/00000000000000000002.json").unlink()
            (workspace / "journal/00000000000000000002.json").unlink()
            result = validate_production_draft_workspace(workspace)
            self.assertFalse(result.passed)
            self.assertTrue(any("contiguous" in error for error in result.errors), result.errors)
        with tempfile.TemporaryDirectory() as directory:
            workspace, _ = _workspace(Path(directory))
            last = workspace / "generations/00000000000000000005.json"
            text = last.read_text()
            last.write_text('{"schemaVersion":2,' + text[1:])
            result = validate_production_draft_workspace(workspace)
            self.assertFalse(result.passed)
            self.assertTrue(any("duplicate JSON" in error for error in result.errors), result.errors)

    def test_retained_evidence_rejects_symbolic_link_directories(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace, _ = _workspace(root)
            (workspace / "source-evidence").rename(root / "outside-evidence")
            try:
                (workspace / "source-evidence").symlink_to(root / "outside-evidence", target_is_directory=True)
            except OSError as exc:
                self.skipTest(str(exc))
            result = validate_production_draft_workspace(workspace)
            self.assertFalse(result.passed)
            self.assertTrue(any("symbolic-link component" in error for error in result.errors), result.errors)

    def test_cli_prepares_new_definition_without_initializing_a_workspace(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = _document(_source(root))
            source_path = root / "sources.json"
            source_path.write_bytes(_text(document))
            output = root / "draft.json"
            command = [sys.executable, "-m", "tools.external_beta.voicebank_production", "prepare-draft", "--strategies", str(source_path),
                       "--repository-root", str(root), "--project-id", "cli-draft", "--operator-id", "producer", "--output", str(output)]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            receipt = json.loads(result.stdout)
            definition = json.loads(output.read_bytes())
            self.assertEqual("DRAFT_DEFINITION_PREPARED", receipt["status"])
            self.assertFalse(receipt["releaseEligible"])
            self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), receipt["sha256"])
            self.assertEqual(("DRAFT", 0), (definition["lifecycle"], definition["lastDurableGeneration"]))
            self.assertEqual("NOT_ASSESSED", definition["sourceStrategies"][0]["coverage"])
            self.assertFalse((root / "workspace").exists())
            original = output.read_bytes()
            repeated = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
            self.assertEqual(2, repeated.returncode)
            self.assertEqual(original, output.read_bytes())
            self.assertEqual([], list(root.glob(".seam-draft-*")))

    def test_cli_draft_validation_discloses_engineering_scope(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, _ = _workspace(Path(directory))
            result = subprocess.run([sys.executable, "-m", "tools.external_beta.voicebank_production", "validate-workspace", "--draft", "--workspace", str(workspace)],
                                    cwd=ROOT, capture_output=True, text=True, check=False)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            value = json.loads(result.stdout)
            self.assertTrue(value["passed"])
            self.assertEqual("engineering", value["evidenceScope"])
            self.assertFalse(value["releaseEligible"])


if __name__ == "__main__":
    unittest.main()
