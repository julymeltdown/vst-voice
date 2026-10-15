"""Actual native intake with synthetic PCM; no real singer or rights approval.

These fixtures never manufacture a saved producer generation or receipt. The
CLI creates, captures and imports; the independent Python reader consumes its
actual output. Two donors deliberately have different PCM and WAV identities.
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
from tools.external_beta.voicebank_production import validate_production_draft_workspace
from tools.voicebank_source_intake.prepare_phone_donors import LABEL_CONTRACT, prepare

ROOT = Path(__file__).resolve().parents[2]
CLI = voicebank_cli(ROOT)
UTC = "2026-10-15T00:00:00Z"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode()


def files(root):
    return {str(p.relative_to(root)): ("link", str(p.readlink())) if p.is_symlink()
            else ("bytes", p.read_bytes()) if p.is_file() else ("directory",)
            for p in root.rglob("*")}


class PreparedDonorIntakeCliTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(CLI.is_file(), "Build the required native CLI")
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.preparation = self.root / "preparation"
        self.preparation.mkdir()
        pcm = b"".join(struct.pack("<h", n * 31 - 1000) for n in range(160))
        with wave.open(str(self.preparation / "original.wav"), "wb") as output:
            output.setnchannels(1); output.setsampwidth(2); output.setframerate(8000)
            output.writeframes(pcm)
        (self.preparation / "original.lab").write_text("0.01 0 aa\n0.02 0 aa\n")
        (self.preparation / "license.txt").write_text("SYNTHETIC ORIGINAL LICENSE — café 한글 🎵; no real rights approval.\n")
        bind = lambda name: {"path": name, "sha256": sha((self.preparation / name).read_bytes())}
        plan = {"schemaVersion": 1, "documentKind": "SourceLabelDonorPlan", "language": "en", "labelContract": LABEL_CONTRACT,
                "license": {**bind("license.txt"), "sourceUrl": "https://example.invalid/synthetic",
                            "attribution": "Synthetic fixture author — no real performer"},
                "sources": [{"id": "fixture", "sourceType": "speech", "audio": bind("original.wav"), "labels": bind("original.lab")}],
                "spans": [{"sourceId": "fixture", "firstLabelIndex": index, "labelCount": 1} for index in (0, 1)]}
        plan_path = self.preparation / "plan.json"
        plan_path.write_bytes(encoded(plan))
        self.prepared = self.preparation / "donors"
        self.packet = prepare(plan_path, self.prepared)
        self.packet_sha = sha((self.prepared / "donors.json").read_bytes())
        self.assertNotEqual(*[row["derivedSha256"] for row in self.packet["proposals"]])
        self.assertNotEqual(*[row["pcmSha256"] for row in self.packet["proposals"]])
        profile = {"profileId": "synthetic-native-intake", "language": "en", "vowels": ["aa"], "consonants": ["m"],
                   "specialPhones": ["pau"], "includeKinds": ["sustain"], "supportedStyles": ["neutral", "soft"],
                   "alternateTakes": 1, "pitchLayers": [69, 72]}
        profile_path = self.preparation / "profile.json"
        profile_path.write_bytes(encoded(profile))
        bundle = self.root / "bundle"
        self.native("new-producer-workspace", bundle, "synthetic-native-intake", "producer", UTC,
                    profile_path, sha(profile_path.read_bytes()))
        self.workspace = bundle / "producer"
        self.declaration = self.preparation / "reviewed-declaration.txt"
        self.declaration.write_text("SYNTHETIC REVIEWED DECLARATION — no real singer or acceptance.\n")
        self.register(self.declaration.read_bytes())
        self.coverage = next(row["coverageKey"] for row in self.project()["unitAssignments"] if row["pitchLayer"] == 69)

    def native(self, *arguments, success=True, match=None):
        result = subprocess.run([str(CLI), *map(str, arguments)], cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(0 if success else 2, result.returncode, result.stderr + result.stdout)
        if not success:
            self.assertEqual("", result.stdout)
            self.assertIn("Prepared donor refused", result.stderr)
        if match:
            self.assertIn(match, result.stderr)
        return result

    def project(self):
        return json.loads((self.workspace / "project.json").read_bytes())

    def register(self, raw, source_id="synthetic-human"):
        self.declaration.write_bytes(raw)
        self.native("register-source", self.workspace, sha((self.workspace / "project.json").read_bytes()), source_id,
                    "human", "pass", "yes", "yes", "no", "no", self.declaration, sha(raw), "producer", UTC)

    def capture(self, index=0, style="neutral", pitch=69, name="request.json"):
        target = self.root / name
        before = files(self.workspace), files(self.preparation)
        result = json.loads(self.native("prepare-donor-intake", self.workspace, self.prepared, self.packet_sha,
                                       self.packet["proposals"][index]["id"], "synthetic-human", self.coverage,
                                       pitch, style, target).stdout)
        self.assertEqual(before, (files(self.workspace), files(self.preparation)))
        self.assertEqual(result["request"], json.loads(target.read_bytes()))
        self.assertEqual(result["requestSha256"], sha(target.read_bytes()))
        self.assertFalse(result["nativeImport"])
        return target, result["request"]

    def import_request(self, path, *, success=True, expected=None, actor="producer", utc=UTC, match=None):
        return self.native("import-prepared-donor", self.workspace, self.prepared, path,
                           expected if expected is not None else sha(path.read_bytes()), actor, utc, success=success, match=match)

    def readers(self):
        before = files(self.workspace)
        result = validate_production_draft_workspace(self.workspace)
        self.assertTrue(result.passed, result.errors)
        quality = json.loads(self.native("inspect-source-quality", self.workspace, "synthetic-human").stdout)
        self.assertFalse(quality["releaseEligible"])
        self.assertFalse(quality["recordedQualityPassingForCurrentMaterial"])
        self.assertEqual(before, files(self.workspace))

    def test_capture_displays_exact_notices_phones_and_assignment_without_import(self):
        _, request = self.capture(style="soft", pitch=72)
        self.assertEqual(("UNAPPROVED", "NOT_QUALIFIED", False, False),
                         tuple(request[key] for key in ("status", "qualification", "releaseEligible", "sourceRightsApproved")))
        self.assertEqual(self.packet["proposals"][0], request["unreviewedProposal"])
        self.assertEqual(["aa"], request["sourcePhones"])
        assignment = request["assignment"]
        self.assertEqual((self.coverage, 72, "soft"), tuple(assignment[k] for k in ("coverageKey", "pitchLayer", "style")))
        self.assertEqual("", assignment["supersedesTakeId"])
        self.assertEqual((self.prepared / "license-source.txt").read_text(), request["originalLicense"]["text"])
        self.assertEqual(self.packet["license"], request["originalLicense"]["source"])
        self.assertEqual((self.prepared / "attribution.txt").read_text(), request["attribution"]["text"])
        self.assertEqual(self.declaration.read_text(), request["reviewedSourceDeclaration"]["text"])
        self.assertNotEqual(request["originalLicense"]["sha256"], request["reviewedSourceDeclaration"]["sha256"])
        self.assertEqual([], self.project()["takes"])
        self.assertEqual([], self.project()["sourceBindings"])

    def test_actual_native_import_two_tones_styles_retake_and_python_interoperation(self):
        original = self.project()
        captures = []
        for index, style in ((0, "neutral"), (1, "soft"), (1, "neutral")):
            path, request = self.capture(index, style, name=f"request-{len(captures)}.json")
            captures.append(request)
            receipt = json.loads(self.import_request(path).stdout)
            self.assertTrue(receipt["nativeImport"])
            self.assertTrue(receipt["durabilityConfirmed"])
            self.assertFalse(receipt["sourceRightsApproved"])
            self.assertFalse(receipt["releaseEligible"])
            self.assertEqual("NOT_APPROVED", receipt["bankAdmission"])
            self.assertEqual("MARKER_REVIEW", receipt["queueState"])
            project = self.project()
            self.assertEqual(request["audio"]["sha256"], receipt["audioSha256"])
            self.assertEqual(request["assignment"]["takeId"], receipt["takeId"])
            self.assertEqual(sha((self.workspace / "project.json").read_bytes()), receipt["projectSha256"])
            self.assertEqual(5, project["schemaVersion"])
            binding = project["sourceBindings"][-1]
            self.assertEqual(request["audio"]["sha256"], binding["rawAssetSha256"])
            self.assertEqual(request["donorId"], binding["preparedDonor"]["donorId"])
            self.assertEqual(request["originalLicense"]["sha256"], binding["preparedDonor"]["originalLicenseSha256"])
            self.assertEqual(request["reviewedSourceDeclaration"]["sha256"], binding["strategy"]["licenseSha256"])
            self.assertEqual([], project["reviews"])
            self.assertEqual([], project["sourceQualityAssessments"])
            self.assertFalse(any(row["markerReviewed"] or row["pitchReviewed"] for row in project["unitAssignments"]))
            self.assertTrue(any(row["takeId"] == receipt["takeId"] and row["kind"] == "take-inspection.v2"
                                for row in project["metadataRevisions"]))
            self.readers()
        self.assertEqual(2, len(project["assets"]))
        self.assertEqual(3, len(project["sourceBindings"]))
        self.assertEqual(3, len(project["takes"]))
        self.assertEqual(captures[0]["assignment"]["takeId"], captures[2]["assignment"]["supersedesTakeId"])
        self.assertEqual("RETAKE", project["takes"][0]["state"])
        self.assertEqual(original["lastDurableGeneration"] + 3, project["lastDurableGeneration"])
        retained = self.workspace / "source-evidence/packets"
        self.assertEqual([self.packet_sha], [p.name for p in retained.iterdir()])
        self.assertEqual({"donors.json", "intake-plan.json", "license-source.txt", "attribution.txt"},
                         {p.name for p in (retained / self.packet_sha).iterdir()})
        shutil.rmtree(self.preparation)
        self.readers()

    def test_rehashed_request_edits_cannot_bypass_displayed_material_or_approval_boundary(self):
        path, original = self.capture()
        mutations = [("extra", lambda r: r.update(unknown=True)), ("schema", lambda r: r.update(schemaVersion=2)),
                     ("approval", lambda r: r.update(status="APPROVED")), ("release", lambda r: r.update(releaseEligible=True)),
                     ("source approval", lambda r: r.update(sourceRightsApproved=True)),
                     ("license", lambda r: r["originalLicense"].update(text="different license")),
                     ("declaration", lambda r: r["reviewedSourceDeclaration"].update(text="different declaration")),
                     ("attribution", lambda r: r["attribution"].update(text="different author")),
                     ("phones", lambda r: r.update(sourcePhones=["jh"])),
                     ("proposal", lambda r: r["unreviewedProposal"].update(phoneticReview="REVIEWED")),
                     ("donor", lambda r: r.update(donorId=self.packet["proposals"][1]["id"])),
                     ("audio", lambda r: r["audio"].update(sha256="0" * 64)),
                     ("take", lambda r: r["assignment"].update(takeId="invented")),
                     ("prompt", lambda r: r["assignment"].update(promptId="invented")),
                     ("supersedes", lambda r: r["assignment"].update(supersedesTakeId="invented")),
                     ("float pitch", lambda r: r["assignment"].update(pitchLayer=69.0)),
                     ("overflow pitch", lambda r: r["assignment"].update(pitchLayer=1 << 40)),
                     ("style", lambda r: r["assignment"].update(style="missing")),
                     ("coverage", lambda r: r["assignment"].update(coverageKey="missing")),
                     ("source", lambda r: r.update(sourceStrategyId="missing"))]
        before = files(self.workspace), files(self.preparation)
        for label, mutate in mutations:
            with self.subTest(label=label):
                changed = copy.deepcopy(original); mutate(changed); path.write_bytes(encoded(changed))
                self.import_request(path, success=False)
                self.assertEqual(before, (files(self.workspace), files(self.preparation)))
        path.write_bytes(encoded(original))
        for expected, actor, utc in (("A" * 64, "producer", UTC), ("0" * 64, "producer", UTC),
                                     (sha(path.read_bytes()), "unregistered", UTC), (sha(path.read_bytes()), "producer", "invalid")):
            with self.subTest(expected=expected, actor=actor, utc=utc):
                self.import_request(path, success=False, expected=expected, actor=actor, utc=utc)
                self.assertEqual(before, (files(self.workspace), files(self.preparation)))
        for raw in (b'{"status":"UNAPPROVED",' + encoded(original).lstrip()[1:],
                    encoded(original).decode().encode("utf-16"), b"\xef\xbb\xbf" + encoded(original),
                    encoded(original).replace(b"UNAPPROVED", b"\xff")):
            path.write_bytes(raw); self.import_request(path, success=False)
            self.assertEqual(before, (files(self.workspace), files(self.preparation)))
        path.unlink(); path.symlink_to(self.declaration)
        self.import_request(path, expected=sha(self.declaration.read_bytes()), success=False)
        self.assertEqual(before, (files(self.workspace), files(self.preparation)))

    def test_stale_capture_and_replay_are_refused_before_reading_preparation(self):
        stale, _ = self.capture(name="stale.json")
        current, _ = self.capture(1, "soft", name="current.json")
        self.import_request(current)
        before = files(self.workspace)
        shutil.rmtree(self.preparation)
        for path in (stale, current):
            self.import_request(path, success=False, match="producer snapshot is stale")
            self.assertEqual(before, files(self.workspace))

    def test_damaged_external_donor_packet_and_declaration_refuse_without_workspace_writes(self):
        path, _ = self.capture()
        before = files(self.workspace)
        for target in [self.prepared / name for name in ("donors.json", "intake-plan.json", "license-source.txt", "attribution.txt",
                                                        self.packet["proposals"][0]["derivedPath"])]:
            raw = target.read_bytes()
            for mode in ("changed", "missing", "linked"):
                with self.subTest(target=target.name, mode=mode):
                    if mode == "changed": target.write_bytes(raw + b"changed")
                    else:
                        target.unlink()
                        if mode == "linked": target.symlink_to(self.declaration)
                    self.import_request(path, success=False)
                    self.assertEqual(before, files(self.workspace))
                    if target.exists() or target.is_symlink(): target.unlink()
                    target.write_bytes(raw)
        marker = self.prepared / "INCOMPLETE"
        marker.write_text("incomplete")
        self.import_request(path, success=False); marker.unlink()
        self.assertEqual(before, files(self.workspace))
        # The legacy importer still requires the original declaration locator
        # until import. Preparation independence begins only after commitment.
        original = self.declaration.read_bytes()
        self.declaration.write_bytes(b"changed declaration")
        self.import_request(path, success=False)
        self.assertEqual(before, files(self.workspace))
        self.declaration.write_bytes(original)
        self.import_request(path)
        self.readers()

    def test_prepare_refuses_implicit_assignments_overwrite_input_paths_and_invalid_notice_utf8(self):
        args = [self.workspace, self.prepared, self.packet_sha, self.packet["proposals"][0]["id"],
                "synthetic-human", self.coverage, "69", "neutral", self.root / "refused.json"]
        before = files(self.workspace), files(self.preparation)
        for position, value in ((2, "A" * 64), (3, "missing"), (4, "missing"), (5, "missing"), (6, "69.0"),
                                (6, "2147483648"), (6, "70"), (7, "missing"),
                                (8, self.workspace / "new.json"), (8, self.prepared / "new.json")):
            with self.subTest(position=position, value=value):
                altered = args.copy(); altered[position] = value
                self.native("prepare-donor-intake", *altered, success=False)
                self.assertEqual(before, (files(self.workspace), files(self.preparation)))
                self.assertFalse((self.root / "refused.json").exists())
        occupied = self.root / "occupied.json"; occupied.write_text("keep me")
        args[-1] = occupied
        self.native("prepare-donor-intake", *args, success=False)
        self.assertEqual("keep me", occupied.read_text())
        self.assertEqual(before, (files(self.workspace), files(self.preparation)))
        for index, bad in enumerate((b"\xff", b"\xc0\x80", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xe2\x82")):
            # Registration is immutable by source ID, even in synthetic tests.
            source_id = f"synthetic-invalid-utf8-{index}"
            self.register(b"Synthetic declaration " + bad, source_id)
            args[4] = source_id
            before = files(self.workspace), files(self.preparation)
            args[-1] = self.root / "refused.json"
            self.native("prepare-donor-intake", *args, success=False, match="UTF-8")
            self.assertEqual(before, (files(self.workspace), files(self.preparation)))
            self.assertFalse(args[-1].exists())
