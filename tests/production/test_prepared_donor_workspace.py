"""Synthetic persistence interoperability; no real singer, rights or bank acceptance.

Uses actual native workspace/source creation, then deliberately constructs saved
schema-5 fixtures. This is a validator test, not another importer acceptance run.
"""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave

from tests.production.native_cli import voicebank_cli
from tests.production.test_production_draft_parity import _generation, _receipt, _text
from tools.external_beta._prepared_donor_evidence import FILE_LIMITS, prepared_packet_facts
from tools.external_beta.voicebank_production import validate_production_draft_workspace
from tools.voicebank_source_intake.prepare_phone_donors import LABEL_CONTRACT, prepare

ROOT = Path(__file__).resolve().parents[2]
CLI = voicebank_cli(ROOT)
UTC = "2026-09-09T10:00:00Z"
sha = lambda raw: hashlib.sha256(raw).hexdigest()


def binding_id(project, binding):
    identity = {"format": "take-source-binding-v2", "projectId": project["projectId"], "takeId": binding["takeId"],
                "audioSha256": binding["rawAssetSha256"], "strategyId": binding["strategy"]["id"],
                "licenseSha256": binding["strategy"]["licenseSha256"], "importerId": binding["importerId"],
                "importedAtUtc": binding["importedAtUtc"], "preparedDonor": binding["preparedDonor"]}
    return "source-" + sha(json.dumps(identity, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode())


def files(root):
    return {str(p.relative_to(root)): ("link", str(p.readlink())) if p.is_symlink() else ("bytes", p.read_bytes())
            for p in root.rglob("*") if p.is_symlink() or p.is_file()}


class PreparedDonorWorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(CLI.is_file(), "Build the native CLI for required cross-language validation")

    def native(self, *arguments, success=True):
        result = subprocess.run([str(CLI), *map(str, arguments)], cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(success, result.returncode == 0, result.stderr + result.stdout)
        return result

    def fixture(self, root, *, schema=5, language="en", source_kind="human"):
        preparation = root / "preparation"; preparation.mkdir()
        with wave.open(str(preparation / "original.wav"), "wb") as output:
            output.setnchannels(1); output.setsampwidth(2); output.setframerate(48000)
            output.writeframes(b"".join(struct.pack("<h", index * 23 - 700) for index in range(64)))
        (preparation / "original.lab").write_text("0.000666666 0 aa\n0.001333333 0 aa\n")
        (preparation / "license.txt").write_text("SYNTHETIC ORIGINAL LICENSE, NO REAL RIGHTS ACCEPTANCE\n")
        bind = lambda name: {"path": name, "sha256": sha((preparation / name).read_bytes())}
        plan = {"schemaVersion": 1, "documentKind": "SourceLabelDonorPlan", "language": "en", "labelContract": LABEL_CONTRACT,
                "license": {**bind("license.txt"), "sourceUrl": "https://example.invalid/synthetic", "attribution": "Synthetic author"},
                "sources": [{"id": "fixture", "sourceType": "speech", "audio": bind("original.wav"), "labels": bind("original.lab")}],
                "spans": [{"sourceId": "fixture", "firstLabelIndex": index, "labelCount": 1} for index in (0, 1)]}
        plan_path = preparation / "plan.json"; plan_path.write_bytes(_text(plan))
        prepared = preparation / "donors"; packet = prepare(plan_path, prepared)
        self.assertNotEqual(packet["proposals"][0]["derivedSha256"], packet["proposals"][1]["derivedSha256"])
        self.assertNotEqual(packet["proposals"][0]["pcmSha256"], packet["proposals"][1]["pcmSha256"])
        packet_sha = sha((prepared / "donors.json").read_bytes())
        profile = {"profileId": "synthetic-packet-parity", "language": language, "vowels": ["aa" if language == "en" else "a"], "consonants": ["m"],
                   "specialPhones": ["pau"], "includeKinds": ["sustain"], "supportedStyles": ["neutral", "soft"],
                   "alternateTakes": 1, "pitchLayers": [69, 72]}
        profile_path = preparation / "profile.json"; profile_path.write_bytes(_text(profile))
        bundle = root / "bundle"
        self.native("new-producer-workspace", bundle, "synthetic-packet-parity", "producer", UTC,
                    profile_path, sha(profile_path.read_bytes()))
        workspace = bundle / "producer"
        project = json.loads((workspace / "project.json").read_bytes())
        declaration = preparation / "reviewed-declaration.txt"
        declaration.write_text("SYNTHETIC REVIEWED DECLARATION; NO REAL SINGER OR APPROVAL\n")
        self.native("register-source", workspace, sha((workspace / "project.json").read_bytes()), "synthetic-human", source_kind,
                    "pass", "yes", "yes", "no", "no", declaration, sha(declaration.read_bytes()), "producer", UTC)
        project = json.loads((workspace / "project.json").read_bytes())
        strategy = project["sourceStrategies"][0]
        retained = workspace / "source-evidence/packets" / packet_sha; retained.mkdir(parents=True)
        for name in FILE_LIMITS:
            shutil.copyfile(prepared / name, retained / name)
        (workspace / f"source-evidence/{strategy['licenseSha256']}.txt").write_bytes(declaration.read_bytes())
        assignments = [next(row for row in project["unitAssignments"]
                            if row["style"] == style and row["pitchLayer"] == 69)
                       for style in ("neutral", "soft")]
        for assignment, proposal in zip(assignments, packet["proposals"]):
            raw = (prepared / proposal["derivedPath"]).read_bytes(); digest = sha(raw)
            relative = f"raw/{digest[:2]}/{digest}.wav"
            asset = workspace / "assets" / relative; asset.parent.mkdir(parents=True, exist_ok=True); asset.write_bytes(raw)
            if all(asset["sha256"] != digest for asset in project["assets"]):
                project["assets"].append({"sha256": digest, "byteSize": len(raw), "relativePath": relative, "kind": "RAW"})
            take = {"takeId": assignment["plannedTakeId"], "promptId": assignment["promptId"],
                    "coverageKey": assignment["coverageKey"], "pitchLayer": 69, "style": assignment["style"],
                    "rawAssetSha256": digest, "derivedRevisionIds": [], "supersedesTakeId": "", "state": "MARKER_REVIEW"}
            binding = {"id": "", "takeId": take["takeId"], "rawAssetSha256": digest, "strategy": copy.deepcopy(strategy),
                       "importerId": "producer", "importedAtUtc": UTC, "licenseSnapshotPath": f"source-evidence/{strategy['licenseSha256']}.txt",
                       "preparedDonor": {"packetSha256": packet_sha, "donorId": proposal["id"], "intakePlanSha256": packet["planSha256"],
                                         "originalLicenseSha256": packet["license"]["sha256"], "attributionSha256": packet["retainedAttributionSha256"]}}
            binding["id"] = binding_id(project, binding); take["sourceBindingId"] = binding["id"]
            project["takes"].append(take); project["sourceBindings"].append(binding)
            project["metadataRevisions"].append(_receipt(take, len(raw), "producer"))
            assignment.update(takeId=take["takeId"], state="MARKER_REVIEW")
            project.update(schemaVersion=schema, lifecycle="EXPERIMENTAL")
            _generation(workspace, project, project["lastDurableGeneration"] + 1, "import", take["takeId"])
        return workspace, project, prepared

    def rewrite(self, workspace, project):
        number = project["lastDurableGeneration"]
        _generation(workspace, project, number, "import", project["takes"][-1]["takeId"])

    def assert_readers(self, workspace, success, *, only_python_error=None):
        before = files(workspace)
        result = validate_production_draft_workspace(workspace)
        self.assertEqual(success, result.passed, result.errors)
        if only_python_error is not None:
            self.assertTrue(result.errors)
            self.assertTrue(all(error.endswith(only_python_error) for error in result.errors), result.errors)
        native = self.native("inspect-source-quality", workspace, "synthetic-human", success=success)
        self.assertEqual(before, files(workspace))
        if success:
            self.assertFalse(json.loads(native.stdout)["releaseEligible"])
            self.assertFalse(json.loads(native.stdout)["recordedQualityPassingForCurrentMaterial"])

    def test_schema_five_retains_independent_notices_after_preparation_disappears(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace, project, prepared = self.fixture(Path(directory))
            self.assert_readers(workspace, True)
            original = prepared.joinpath("license-source.txt").read_bytes()
            self.assertNotEqual(project["sourceBindings"][0]["strategy"]["licenseSha256"], sha(original))
            shutil.rmtree(prepared.parent)
            self.assert_readers(workspace, True)
            self.assertEqual(2, len(project["sourceBindings"]))
            self.assertEqual(2, len(project["assets"]))
            self.assertEqual([], project["reviews"])

    def test_present_donor_with_different_pcm_cannot_replace_captured_audio(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace, project, prepared = self.fixture(Path(directory))
            self.assert_readers(workspace, True)
            print("[PYTHON-SOURCE-SEMANTICS] distinct-donor control: PASS")
            packet = json.loads((prepared / "donors.json").read_bytes())
            first, second = project["sourceBindings"]
            self.assertNotEqual(first["rawAssetSha256"], second["rawAssetSha256"])
            self.assertIn(first["preparedDonor"]["donorId"], {row["id"] for row in packet["proposals"]})
            second["preparedDonor"]["donorId"] = first["preparedDonor"]["donorId"]
            second["id"] = binding_id(project, second)
            project["takes"][-1]["sourceBindingId"] = second["id"]
            self.rewrite(workspace, project)
            diagnostic = f"retained prepared donor differs from captured take {second['takeId']}"
            self.assert_readers(workspace, False, only_python_error=diagnostic)
            print("[PYTHON-SOURCE-SEMANTICS] present donor different audio: " + diagnostic)

    def test_consistent_histories_isolate_schema_language_and_source_kind_guards(self):
        variants = [("schema-four", {"schema": 4}), ("Japanese", {"language": "ja"}),
                    ("procedural-source", {"source_kind": "procedural"})]
        diagnostic = "prepared donor requires schema 5 English human-recording identity and five exact fields"
        for name, options in variants:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                workspace, project, _ = self.fixture(Path(directory), **options)
                generations = [json.loads(path.read_bytes()) for path in sorted((workspace / "generations").glob("*.json"))]
                self.assertEqual([1, 2, 3, 4], [state["lastDurableGeneration"] for state in generations])
                self.assertEqual({project["language"]}, {state["language"] for state in generations})
                schemas = [state["schemaVersion"] for state in generations]
                self.assertEqual(sorted(schemas), schemas)
                if name == "schema-four": self.assertEqual({4}, set(schemas))
                for state in generations:
                    if state["sourceStrategies"]:
                        self.assertEqual(project["sourceStrategies"], state["sourceStrategies"])
                    for binding in state["sourceBindings"]:
                        self.assertEqual(project["sourceStrategies"][0], binding["strategy"])
                # A deleted guard would make Python accept this complete,
                # consistently authored history. No history error may stand in
                # for the intended clause; native must independently refuse.
                self.assert_readers(workspace, False, only_python_error=diagnostic)
                print("[PYTHON-SOURCE-SEMANTICS] " + name + ": " + diagnostic)

    def test_exact_prepared_fields_and_v2_identity_refuse_malformed_models(self):
        mutations = [("missing " + key, lambda b, key=key: b["preparedDonor"].pop(key))
                     for key in ("packetSha256", "donorId", "intakePlanSha256", "originalLicenseSha256", "attributionSha256")]
        mutations += [("extra", lambda b: b["preparedDonor"].update(extra="unknown")),
                      ("null", lambda b: b.update(preparedDonor=None)), ("array", lambda b: b.update(preparedDonor=[])),
                      ("upper digest", lambda b: b["preparedDonor"].update(packetSha256="A" * 64)),
                      ("unsafe id", lambda b: b["preparedDonor"].update(donorId="../other")),
                      ("wrong v2 id", lambda b: b.update(id="source-" + "0" * 64))]
        for label, mutate in mutations:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                workspace, project, _ = self.fixture(Path(directory)); binding = project["sourceBindings"][-1]
                mutate(binding); project["takes"][-1]["sourceBindingId"] = binding["id"]
                self.rewrite(workspace, project); self.assert_readers(workspace, False)

    def test_resigned_wrong_donor_and_notice_bindings_still_refuse(self):
        for key in ("donorId", "intakePlanSha256", "originalLicenseSha256", "attributionSha256"):
            with self.subTest(key=key), tempfile.TemporaryDirectory() as directory:
                workspace, project, _ = self.fixture(Path(directory)); binding = project["sourceBindings"][-1]
                binding["preparedDonor"][key] = "absent_l00002_n1" if key == "donorId" else "0" * 64
                binding["id"] = binding_id(project, binding); project["takes"][-1]["sourceBindingId"] = binding["id"]
                self.rewrite(workspace, project); self.assert_readers(workspace, False)

    def test_retained_notice_damage_and_linked_parents_refuse_without_writes(self):
        for name in (*FILE_LIMITS, "packets", "packet-directory", "INCOMPLETE"):
            for mode in (range(3) if name in FILE_LIMITS else range(1)):
                with self.subTest(name=name, mode=mode), tempfile.TemporaryDirectory() as directory:
                    workspace, project, _ = self.fixture(Path(directory))
                    self.assert_readers(workspace, True)
                    packet = workspace / "source-evidence/packets" / project["sourceBindings"][0]["preparedDonor"]["packetSha256"]
                    target = packet / name
                    if name in FILE_LIMITS:
                        if mode == 0: target.unlink()
                        elif mode == 1: target.write_bytes(b"different retained notice")
                        else:
                            backup = target.with_suffix(".backup"); target.rename(backup); target.symlink_to(backup)
                    elif name == "INCOMPLETE": target.write_bytes(b"incomplete")
                    else:
                        target = packet.parent if name == "packets" else packet
                        backup = target.with_name(target.name + ".backup"); target.rename(backup); target.symlink_to(backup, target_is_directory=True)
                    self.assert_readers(workspace, False)

    def test_legacy_future_language_and_source_kind_cannot_bypass_prepared_rules(self):
        for mode in ("legacy", "future", "language", "source kind"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                workspace, project, _ = self.fixture(Path(directory))
                if mode == "legacy": project["schemaVersion"] = 4
                elif mode == "future": project["schemaVersion"] = 6
                elif mode == "language": project["language"] = "ja"
                else: project["sourceBindings"][-1]["strategy"]["kind"] = "PROCEDURAL_SYNTHESIS"
                self.rewrite(workspace, project); self.assert_readers(workspace, False)

    def test_owned_metadata_enforces_wire_types_and_consistent_range_bounds_without_wavs(self):
        variants = [{"sampleRate": 4000, "durationSeconds": 0.008}, {"sampleRate": 200000, "durationSeconds": 0.00016},
                    {"frames": 0, "endFrameExclusive": 0, "durationSeconds": 0},
                    {"frames": 33560000, "endFrameExclusive": 33560000, "durationSeconds": 33560000 / 48000},
                    {"startFrame": -1, "endFrameExclusive": 31}, {"sampleRate": 48000.0}, {"nonzeroPcm": 1}]
        with tempfile.TemporaryDirectory() as directory:
            _, _, prepared = self.fixture(Path(directory))
            held = {name: (prepared / name).read_bytes() for name in FILE_LIMITS}
            packet = json.loads(held["donors.json"])
            shutil.rmtree(prepared.parent)
            self.assertEqual(2, len(prepared_packet_facts(held, sha(held["donors.json"]))["donors"]))
            for mutation in variants:
                with self.subTest(mutation=mutation):
                    changed = copy.deepcopy(packet); changed["proposals"][0].update(mutation)
                    bad = {**held, "donors.json": _text(changed)}
                    with self.assertRaisesRegex(ValueError, "Prepared donor"):
                        prepared_packet_facts(bad, sha(bad["donors.json"]))

    def test_retained_packet_parser_refuses_non_native_json_forms(self):
        for mode in ("duplicate key", "int64 overflow", "nonfinite", "UTF16", "BOM", "unpaired surrogate", "depth"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                workspace, project, prepared = self.fixture(Path(directory))
                held = {name: (prepared / name).read_bytes() for name in FILE_LIMITS}
                raw = held["donors.json"]
                packet = json.loads(raw)
                if mode == "duplicate key": raw = b'{"schemaVersion":1,' + raw.lstrip()[1:]
                elif mode == "UTF16": raw = raw.decode().encode("utf-16")
                elif mode == "BOM": raw = b"\xef\xbb\xbf" + raw
                else:
                    value = {"int64 overflow": 1 << 63, "nonfinite": float("inf"),
                             "unpaired surrogate": "\ud800", "depth": [[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]}[mode]
                    packet["proposals"][0]["sourceAnnotations"] = value
                    raw = _text(packet)
                digest = sha(raw)
                retained = workspace / "source-evidence/packets" / digest; retained.mkdir()
                for name in FILE_LIMITS:
                    (retained / name).write_bytes(raw if name == "donors.json" else held[name])
                binding = project["sourceBindings"][-1]
                binding["preparedDonor"]["packetSha256"] = digest
                binding["id"] = binding_id(project, binding)
                project["takes"][-1]["sourceBindingId"] = binding["id"]
                self.rewrite(workspace, project)
                self.assert_readers(workspace, False)

    def test_schema_five_history_keeps_language_style_and_source_ownership_immutable(self):
        for field in ("language", "style", "source"):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                workspace, project, _ = self.fixture(Path(directory))
                if field == "language": project["language"] = "ja"
                elif field == "style":
                    project["takes"][-1]["style"] = "changed"
                    project["unitAssignments"][-1]["style"] = "changed"
                else: project["sourceBindings"][-1]["preparedDonor"]["donorId"] = "changed"
                _generation(workspace, project, project["lastDurableGeneration"] + 1, "save", project["projectId"])
                result = validate_production_draft_workspace(workspace)
                self.assertFalse(result.passed)
                self.assertTrue(any("immutable" in item or "rewrites" in item for item in result.errors), result.errors)
