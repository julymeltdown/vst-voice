"""The Python mirror of the C++ take-inspection.v2 receipt: same policies, outcomes, bounds and bytes.

Every receipt here describes synthetic fixture audio. It is technical signal evidence only: no listening
review, singer qualification or release claim is made or implied.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.external_beta._production_draft_validation import (
    TAKE_INSPECTION_KIND, TAKE_QC_APPLICABLE, canonical_take_evidence, take_check_outcomes, take_inspection_errors,
    take_qc_policy)
from tools.external_beta.voicebank_production import prepare_production_draft_definition, validate_production_draft_workspace
from tools.voicebank_script_generator.draft_inventory import generate_draft_inventory

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "build/release/seam_voicebank_cli"
BAKE = ROOT / "docs/implementation/evidence/nasal-consonant-2026-09-09/ma-bake"
CANDIDATE = BAKE / "candidates/00000000000153d9-00000000000153da"
RECIPE = BAKE / "recipes/7361e7c1d112ba26cf07284548c14866a865195f52d7737e4352b38382d8d2d2.json"
TAKE_SHA = "f67deb2dbe7dfdeba74988b94854973157e32f9d6bcec1b73384841dcc3eecff"
ASSETS = {TAKE_SHA: {"sha256": TAKE_SHA, "byteSize": 96044, "kind": "RAW"}}
VOICED_PROMPT = "P00007-cf35e43e10f5a1d1351982afe2ad6f98208814c74da711d9b2e05f1f9e425030"
BREATH_PROMPT = "P00001-e3a730258f58d91a794af5204201e708539497f42454fce0be59b76e06da3c5c"

# Written by seam_voicebank_cli import-procedural, built from 4aab93b2, for the committed "ma" bake admitted as
# cv:m:a at MIDI 60 and as breath:br. The bake sings MIDI 69, so the voiced root pitch fails, and a sung syllable
# is not a breath. These are the C++ bytes, %.17g doubles and the integer token of a whole double (voicedShare 1)
# included.
VOICED_RECEIPT = (
    '{"binding":{"coverageKey":"cv:m:a","pitchLayer":60,"promptId":"' + VOICED_PROMPT + '","takeId":"'
    + VOICED_PROMPT + '-t01"},"byteSize":96044,"checks":{"clipping":"PASS","dcOffset":"PASS","finite":"PASS",'
    '"format":"PASS","quiet":"INAPPLICABLE","rootPitch":"FAIL","signalPresent":"PASS","unvoiced":"INAPPLICABLE"},'
    '"inspectorId":"seam.take-inspector","inspectorVersion":"2","measurements":{"analyzedRootMidi":69,'
    '"bitsPerSample":32,"channels":1,"clippedSamples":0,"dcOffset":-9.4434834822028823e-07,"expectedRootMidi":60,'
    '"frameCount":24000,"nonFiniteSamples":0,"peak":0.027358368039131165,"rms":0.0097459917988710881,'
    '"rootPitchDeviationCents":899.86035985351612,"sampleRate":48000,"voicedShare":null},"policy":"voiced",'
    '"policyVersion":1,"schemaVersion":2,"status":"SIGNAL_CHECKS_NEED_REVIEW","takeSha256":"' + TAKE_SHA + '"}')
BREATH_RECEIPT = (
    '{"binding":{"coverageKey":"breath:br","pitchLayer":60,"promptId":"' + BREATH_PROMPT + '","takeId":"'
    + BREATH_PROMPT + '-t01"},"byteSize":96044,"checks":{"clipping":"PASS","dcOffset":"PASS","finite":"PASS",'
    '"format":"PASS","quiet":"INAPPLICABLE","rootPitch":"INAPPLICABLE","signalPresent":"PASS","unvoiced":"FAIL"},'
    '"inspectorId":"seam.take-inspector","inspectorVersion":"2","measurements":{"analyzedRootMidi":null,'
    '"bitsPerSample":32,"channels":1,"clippedSamples":0,"dcOffset":-9.4434834822028823e-07,"expectedRootMidi":null,'
    '"frameCount":24000,"nonFiniteSamples":0,"peak":0.027358368039131165,"rms":0.0097459917988710881,'
    '"rootPitchDeviationCents":null,"sampleRate":48000,"voicedShare":1},"policy":"breath","policyVersion":1,'
    '"schemaVersion":2,"status":"SIGNAL_CHECKS_NEED_REVIEW","takeSha256":"' + TAKE_SHA + '"}')


def _take(coverage_key: str, pitch_layer: int, prompt: str) -> dict:
    return {"takeId": prompt + "-t01", "promptId": prompt, "coverageKey": coverage_key, "pitchLayer": pitch_layer,
            "rawAssetSha256": TAKE_SHA}


VOICED_TAKE = _take("cv:m:a", 60, VOICED_PROMPT)
BREATH_TAKE = _take("breath:br", 60, BREATH_PROMPT)


def _revision(text: str, take_id: str) -> dict:
    digest = hashlib.sha256(text.encode()).hexdigest()
    return {"revisionId": "take-inspection-" + digest[:32], "takeId": take_id, "rawAssetSha256": TAKE_SHA,
            "kind": TAKE_INSPECTION_KIND, "values": {"evidenceJson": text, "evidenceSha256": digest},
            "operatorId": "producer", "performedAtUtc": "2026-09-28T00:06:00Z"}


def _edit(mutate):
    """A forgery that edits the decoded evidence and writes it back in canonical form."""
    def forge(text: str) -> str:
        evidence = json.loads(text)
        mutate(evidence)
        return canonical_take_evidence(evidence)
    return forge


def _as_breath(evidence: dict) -> None:
    """Re-judge the voiced take as breath, consistently, so that only the policy choice is wrong."""
    measured = evidence["measurements"]
    measured.update(expectedRootMidi=None, analyzedRootMidi=None, rootPitchDeviationCents=None, voicedShare=0.25)
    evidence["policy"] = "breath"
    evidence["checks"] = take_check_outcomes("breath", measured)
    evidence["status"] = "SIGNAL_CHECKS_NEED_REVIEW" if "FAIL" in evidence["checks"].values() else "SIGNAL_CHECKS_PASSED"


# Each forgery of the voiced cv:m:a receipt at MIDI 60 and the cause it must be refused for.
FORGERIES = (
    ("status flipped", _edit(lambda e: e.update(status="SIGNAL_CHECKS_PASSED")),
     "status does not match its check outcomes"),
    ("failed pitch relabelled as passing",
     _edit(lambda e: (e["checks"].update(rootPitch="PASS"), e.update(status="SIGNAL_CHECKS_PASSED"))),
     "outcomes do not follow from their measurements"),
    ("measured deviation rewritten", _edit(lambda e: e["measurements"].update(rootPitchDeviationCents=12.5)),
     "outcomes do not follow from their measurements"),
    ("rebound to another pitch layer",
     _edit(lambda e: (e["binding"].update(pitchLayer=66), e["measurements"].update(expectedRootMidi=66))),
     "made for a different assignment"),
    ("judged under the breath policy", _edit(_as_breath), "QC policy that does not apply to its unit"),
    ("other bytes named", _edit(lambda e: e.update(takeSha256="0" * 64)), "does not describe the stored take bytes"),
    ("byte size misstated", _edit(lambda e: e.update(byteSize=e["byteSize"] + 1)), "byte size differs from the stored asset"),
    ("peak widened past float precision",
     _edit(lambda e: e["measurements"].update(peak=math.nextafter(e["measurements"]["peak"], 1.0))),
     "measurements are out of range"),
    ("breath share smuggled into a voiced receipt", _edit(lambda e: e["measurements"].update(voicedShare=0.0)),
     "measurements its policy does not use"),
    ("pitch fields unpaired", _edit(lambda e: e["measurements"].update(analyzedRootMidi=None)),
     "measurements its policy does not use"),
    ("older inspector", _edit(lambda e: e.update(inspectorVersion="1")), "not a current inspector"),
    ("unknown field", _edit(lambda e: e.update(note="looks fine")), "fields are incomplete or unknown"),
    ("pretty printed", lambda text: json.dumps(json.loads(text), indent=1, sort_keys=True), "not in canonical form"),
    ("escaped letter", lambda text: text.replace('"policy":"voiced"', '"policy":"\\u0076oiced"'), "not in canonical form"),
    ("version written as a float", lambda text: text.replace('"schemaVersion":2', '"schemaVersion":2.0'),
     "not a current inspector"),
    ("version written as a boolean", lambda text: text.replace('"policyVersion":1', '"policyVersion":true'),
     "not a current inspector"),
    ("frame count beyond int64", lambda text: re.sub(r'"frameCount":\d+', '"frameCount":' + str(1 << 64), text),
     "measurements are out of range"),
    ("sample rate beyond the reader's range", _edit(lambda e: e["measurements"].update(sampleRate=400000)),
     "measurements are out of range"),
    ("more clipped samples than frames",
     _edit(lambda e: e["measurements"].update(clippedSamples=e["measurements"]["frameCount"] + 1)),
     "measurements are out of range"),
    ("duplicate field", lambda text: text[:-1] + ',"status":"SIGNAL_CHECKS_NEED_REVIEW"}', "not bounded JSON"),
)


class TakeQcPolicyTest(unittest.TestCase):
    def test_policy_follows_the_coverage_key_and_refuses_ambiguous_keys(self) -> None:
        for key in ("sustain:a", "cv:k:a", "vc:a:k", "vv:a:i", "release:a:R", "glottal-attack:glottal:a", "special:N",
                    "vowel:a", "special:" + ":".join(["N"] * 64)):
            self.assertEqual("voiced", take_qc_policy(key), key)
        for key, policy in (("breath:br", "breath"), ("special:br", "breath"), ("special:pau", "pause"),
                            ("special:sil", "pause"), ("special:cl", "closure"), ("special:R", "closure"),
                            ("special:glottal", "closure"), ("special:cl:R", "closure")):
            self.assertEqual(policy, take_qc_policy(key), key)
        for key in ("", "sustain", ":a", "sustain:", "cv::a", "cv:k a", "special:pau:br", "special:cl:a",
                    "cv:" + "k" * 5000, "cv:" + "k" * 129 + ":a", "cv:k\x7fa", "special:" + ":".join(["N"] * 65),
                    "cv:\ud800", None, 7):
            self.assertIsNone(take_qc_policy(key), repr(key)[:40])

    def test_applicability_is_explicit_and_never_an_implied_pass(self) -> None:
        self.assertEqual({"format", "finite", "clipping", "dcOffset"}, set.intersection(*TAKE_QC_APPLICABLE.values()))
        self.assertEqual({"rootPitch"}, TAKE_QC_APPLICABLE["voiced"] - TAKE_QC_APPLICABLE["breath"])
        self.assertEqual({"unvoiced"}, TAKE_QC_APPLICABLE["breath"] - TAKE_QC_APPLICABLE["voiced"])
        self.assertEqual(TAKE_QC_APPLICABLE["pause"], TAKE_QC_APPLICABLE["closure"])
        self.assertNotIn("signalPresent", TAKE_QC_APPLICABLE["pause"])

    def test_outcomes_use_the_version_one_thresholds_at_their_boundaries(self) -> None:
        edge = {"sampleRate": 48000, "channels": 1, "bitsPerSample": 24, "frameCount": 4800, "nonFiniteSamples": 0,
                "clippedSamples": 0, "peak": 0.03, "rms": 0.003, "dcOffset": 0.01, "expectedRootMidi": 60,
                "analyzedRootMidi": 60, "rootPitchDeviationCents": -80.0, "voicedShare": 0.5}
        self.assertEqual({"format": "PASS", "finite": "PASS", "clipping": "PASS", "dcOffset": "PASS",
                          "signalPresent": "PASS", "quiet": "INAPPLICABLE", "unvoiced": "INAPPLICABLE", "rootPitch": "PASS"},
                         take_check_outcomes("voiced", edge))
        self.assertEqual("PASS", take_check_outcomes("pause", edge)["quiet"])
        self.assertEqual("PASS", take_check_outcomes("breath", edge)["unvoiced"])
        beyond = dict(edge, bitsPerSample=16, dcOffset=-0.0100001, rms=1.0e-4, rootPitchDeviationCents=80.0001)
        self.assertEqual(("FAIL",) * 4, tuple(take_check_outcomes("voiced", beyond)[name]
                                              for name in ("format", "dcOffset", "signalPresent", "rootPitch")))
        self.assertEqual("FAIL", take_check_outcomes("closure", dict(edge, peak=0.0300001))["quiet"])
        self.assertEqual("FAIL", take_check_outcomes("breath", dict(edge, voicedShare=None))["unvoiced"])
        self.assertEqual("FAIL", take_check_outcomes("voiced", dict(edge, rootPitchDeviationCents=None))["rootPitch"])
        self.assertEqual(("FAIL", "FAIL"), tuple(take_check_outcomes("pause", dict(edge, nonFiniteSamples=1,
                                                                                    clippedSamples=1))[name]
                                                  for name in ("finite", "clipping")))


class TakeInspectionReceiptTest(unittest.TestCase):
    def test_cpp_receipts_are_accepted_and_reencoded_byte_for_byte(self) -> None:
        for text, take in ((VOICED_RECEIPT, VOICED_TAKE), (BREATH_RECEIPT, BREATH_TAKE)):
            self.assertEqual([], take_inspection_errors(_revision(text, take["takeId"]), take, ASSETS, "fixture"))
            self.assertEqual(text, canonical_take_evidence(json.loads(text)))

    def test_forgeries_are_refused_for_their_own_cause(self) -> None:
        for name, forge, message in FORGERIES:
            with self.subTest(name):
                forged = forge(VOICED_RECEIPT)
                self.assertNotEqual(VOICED_RECEIPT, forged)
                errors = take_inspection_errors(_revision(forged, VOICED_TAKE["takeId"]), VOICED_TAKE, ASSETS, "fixture")
                self.assertEqual(1, len(errors), errors)
                self.assertIn(message, errors[0])

    def test_the_envelope_names_its_own_digest(self) -> None:
        revision = _revision(VOICED_RECEIPT, VOICED_TAKE["takeId"])
        for mutate in (lambda row: row["values"].update(evidenceJson=VOICED_RECEIPT.replace("FAIL", "PASS", 1)),
                       lambda row: row.update(revisionId="take-inspection-" + "0" * 32),
                       lambda row: row["values"].update(evidenceSha256=row["values"]["evidenceSha256"].upper()),
                       lambda row: row["values"].update(extra="x"),
                       lambda row: row.update(values="not an object")):
            forged = copy.deepcopy(revision)
            mutate(forged)
            self.assertEqual(["fixture take inspection receipt digest or identity is invalid"],
                             take_inspection_errors(forged, VOICED_TAKE, ASSETS, "fixture"))

    def test_the_receipt_describes_its_own_take_and_stored_asset(self) -> None:
        revision = _revision(VOICED_RECEIPT, VOICED_TAKE["takeId"])
        for field, value, message in (("pitchLayer", 66, "different assignment"),
                                      ("pitchLayer", 60.0, "different assignment"),
                                      ("coverageKey", "special:pau", "different assignment"),
                                      ("rawAssetSha256", "1" * 64, "stored take bytes")):
            take = dict(VOICED_TAKE, **{field: value})
            errors = take_inspection_errors(revision, take, ASSETS, "fixture")
            self.assertEqual(1, len(errors), errors)
            self.assertIn(message, errors[0])
        for assets in ({}, {TAKE_SHA: {"byteSize": 96043}}, {TAKE_SHA: {"byteSize": True}}):
            self.assertIn("byte size differs", take_inspection_errors(revision, VOICED_TAKE, assets, "fixture")[0])


@unittest.skipUnless(CLI.is_file(), "Build the actual production CLI for cross-language parity")
class CppTakeInspectionParityTest(unittest.TestCase):
    """Receipts written by the C++ CLI under every policy, and C++ and Python refusing the same forgeries."""

    UNITS = (("breath:br", 60), ("special:pau", 60), ("special:cl", 60), ("cv:m:a", 69), ("cv:m:a", 60))

    @staticmethod
    def _run(*arguments: str) -> subprocess.CompletedProcess:
        return subprocess.run([str(CLI), *arguments], capture_output=True, text=True, timeout=60)

    def _producer(self, root: Path) -> tuple[Path, dict]:
        """A procedural producer that admitted the committed bake into one unit of every QC policy."""
        inventory = generate_draft_inventory({"profileId": "take-qc-parity", "vowels": ["a"], "consonants": ["m"],
            "specialPhones": ["pau", "cl"], "includeKinds": ["cv", "breath", "special"], "alternateTakes": 1,
            "pitchLayers": [60, 69], "requestedRange": {"minMidi": 60, "maxMidi": 72}})
        definition = root / "definition.json"
        draft = prepare_production_draft_definition(inventory, None, project_id="take-qc-parity", operator_id="producer")
        definition.write_bytes((json.dumps(draft, sort_keys=True, indent=2) + "\n").encode())
        workspace = root / "workspace"
        created = self._run("init-production", str(workspace), str(definition),
                            hashlib.sha256(definition.read_bytes()).hexdigest(), "producer", "2026-09-28T00:00:00Z")
        self.assertEqual(0, created.returncode, created.stderr)
        license_path = root / "license.txt"
        license_path.write_text("SYNTHETIC TEST ONLY: procedural fixture, no singer qualification")
        registered = self._run("register-source", str(workspace), json.loads(created.stdout)["projectSha256"],
                               "procedural-a", "procedural", "pass", "yes", "yes", "no", "no", str(license_path),
                               hashlib.sha256(license_path.read_bytes()).hexdigest(), "producer", "2026-09-28T00:01:00Z")
        self.assertEqual(0, registered.returncode, registered.stderr)
        units = {(row["coverageKey"], row["pitchLayer"]): row
                 for row in json.loads((workspace / "project.json").read_text())["unitAssignments"]}
        for minute, unit in enumerate(self.UNITS, start=2):
            row = units[unit]
            imported = self._run("import-procedural", str(workspace), str(CANDIDATE) + ".json", str(CANDIDATE) + ".wav",
                                 str(RECIPE), row["plannedTakeId"], row["promptId"], row["coverageKey"],
                                 str(row["pitchLayer"]), "producer", f"2026-09-28T00:{minute:02}:00Z")
            self.assertEqual(0, imported.returncode, imported.stderr)
        return workspace, inventory

    @staticmethod
    def _rewrite(source: Path, target: Path, generation: int, revision_id: str, text: str,
                 keep_digest: bool = False) -> Path:
        """A copy whose newest generation carries this receipt text, with its journal rehashed to match."""
        shutil.copytree(source, target)
        project = json.loads((source / "project.json").read_text())
        row = next(item for item in project["metadataRevisions"] if item["revisionId"] == revision_id)
        digest = row["values"]["evidenceSha256"] if keep_digest else hashlib.sha256(text.encode()).hexdigest()
        row.update(revisionId="take-inspection-" + digest[:32], values={"evidenceJson": text, "evidenceSha256": digest})
        payload = (json.dumps(project, sort_keys=True, indent=2) + "\n").encode()
        name = f"{generation:020}.json"
        journal = json.loads((source / "journal" / name).read_text())
        journal["projectSha256"] = hashlib.sha256(payload).hexdigest()
        (target / "generations" / name).write_bytes(payload)
        (target / "journal" / name).write_bytes((json.dumps(journal, sort_keys=True, indent=2) + "\n").encode())
        (target / "project.json").write_bytes(payload)
        return target

    def _cpp_accepts(self, workspace: Path) -> bool:
        return self._run("inspect-source-quality", str(workspace), "procedural-a").returncode == 0

    def test_cli_receipts_match_the_mirror_and_both_refuse_the_same_forgeries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace, inventory = self._producer(root)
            verified = validate_production_draft_workspace(workspace, inventory)
            self.assertTrue(verified.passed, verified.errors)
            project = json.loads((workspace / "project.json").read_text())
            takes = {take["takeId"]: take for take in project["takes"]}
            assets = {asset["sha256"]: asset for asset in project["assets"]}
            receipts = [row for row in project["metadataRevisions"] if row["kind"] == TAKE_INSPECTION_KIND]
            judged = {}
            for receipt in receipts:
                take, text = takes[receipt["takeId"]], receipt["values"]["evidenceJson"]
                evidence = json.loads(text)
                judged[take["coverageKey"], take["pitchLayer"]] = evidence["policy"], evidence["checks"]
                self.assertEqual([], take_inspection_errors(receipt, take, assets, "cli"))
                self.assertEqual(text, canonical_take_evidence(evidence))
            self.assertEqual(set(self.UNITS), set(judged))
            self.assertEqual({"breath:br": "breath", "special:pau": "pause", "special:cl": "closure", "cv:m:a": "voiced"},
                             {unit[0]: policy for unit, (policy, _) in judged.items()})
            self.assertEqual("PASS", judged["cv:m:a", 69][1]["rootPitch"])
            self.assertEqual("FAIL", judged["cv:m:a", 60][1]["rootPitch"])
            self.assertEqual("FAIL", judged["breath:br", 60][1]["unvoiced"])
            self.assertEqual(("FAIL", "FAIL"), (judged["special:pau", 60][1]["quiet"], judged["special:cl", 60][1]["quiet"]))

            # Only the newest generation can be rewritten without also rewriting history, so the forgeries
            # target the receipt its import appended: the voiced unit at MIDI 60.
            generation = project["lastDurableGeneration"]
            subject = json.loads((workspace / "journal" / f"{generation:020}.json").read_text())["subjectId"]
            self.assertEqual(("cv:m:a", 60), (takes[subject]["coverageKey"], takes[subject]["pitchLayer"]))
            newest = next(row for row in receipts if row["takeId"] == subject)
            original = newest["values"]["evidenceJson"]
            control = self._rewrite(workspace, root / "control", generation, newest["revisionId"], original)
            self.assertTrue(self._cpp_accepts(control))
            self.assertTrue(validate_production_draft_workspace(control, inventory).passed)
            for index, (name, forge, message) in enumerate(FORGERIES):
                with self.subTest(name):
                    target = self._rewrite(workspace, root / f"forged-{index}", generation, newest["revisionId"],
                                           forge(original))
                    self.assertFalse(self._cpp_accepts(target))
                    result = validate_production_draft_workspace(target, inventory)
                    self.assertFalse(result.passed)
                    self.assertTrue(any(message in error for error in result.errors), result.errors)
            stale = self._rewrite(workspace, root / "stale-digest", generation, newest["revisionId"],
                                  FORGERIES[0][1](original), keep_digest=True)
            self.assertFalse(self._cpp_accepts(stale))
            self.assertTrue(any("digest or identity" in error
                                for error in validate_production_draft_workspace(stale, inventory).errors))


if __name__ == "__main__":
    unittest.main()
