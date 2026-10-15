from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from unittest.mock import patch

from tools.voicebank_source_intake.prepare_phone_donors import LABEL_CONTRACT, prepare


class PhoneDonorIntakeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.audio = self.root / "speech.wav"
        self.pcm = b"".join(struct.pack("<h", index - 400) for index in range(800))
        with wave.open(str(self.audio), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(8000)
            wav.writeframes(self.pcm)
        self.labels = self.root / "speech.lab"
        self.labels.write_text("#\n0.0125 125 s\n0.02503125 125 aa\n0.043 125 jh\n0.080 125 pau\n")
        self.license = self.root / "license.txt"
        self.license.write_text("Synthetic fixture only; no performer or production approval.\n")
        self.plan_path = self.root / "plan.json"
        self.plan = {"schemaVersion": 1, "documentKind": "SourceLabelDonorPlan", "language": "en",
                     "labelContract": copy.deepcopy(LABEL_CONTRACT),
                     "license": {**self.binding(self.license), "sourceUrl": "https://example.invalid/license",
                                 "attribution": "Synthetic fixture"},
                     "sources": [{"id": "fixture", "sourceType": "speech",
                                  "audio": self.binding(self.audio), "labels": self.binding(self.labels)}],
                     "spans": [{"sourceId": "fixture", "firstLabelIndex": first, "labelCount": count}
                               for first, count in [(0, 1), (1, 1), (2, 1), (0, 2)]]}
        self.save()

    def binding(self, path: Path) -> dict:
        return {"path": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}

    def save(self) -> None:
        self.plan_path.write_text(json.dumps(self.plan))

    def refuse(self, match: str) -> None:
        self.save()
        output = self.root / "refused"
        with self.assertRaisesRegex(ValueError, match):
            prepare(self.plan_path, output)
        self.assertFalse(output.exists())

    def test_preserves_pcm_exact_boundaries_annotations_and_unapproved_state(self) -> None:
        held = {p: p.read_bytes() for p in (self.audio, self.labels, self.license)}
        output = self.root / "one"
        result = prepare(self.plan_path, output)
        intervals = [(0, 100), (100, 201), (201, 344), (0, 201)]
        self.assertEqual(4, len(result["proposals"]))
        self.assertEqual("UNAPPROVED", result["status"])
        self.assertEqual("NOT_APPROVED", result["bankAdmission"])
        self.assertEqual("NOT_ESTABLISHED", result["performerRights"])
        self.assertNotIn(str(self.root), json.dumps(result))
        self.assertEqual(["jh"], result["proposals"][2]["sourcePhones"])
        self.assertEqual("125", result["proposals"][2]["sourceAnnotations"][0]["opaqueColumn"])
        for row, (start, end) in zip(result["proposals"], intervals):
            self.assertEqual((start, end), (row["startFrame"], row["endFrameExclusive"]))
            self.assertEqual("NOT_ASSIGNED", row["assignment"])
            self.assertEqual("NOT_REVIEWED", row["phoneticReview"])
            self.assertNotIn("observedNearestMidi", row)
            with wave.open(str(output / row["derivedPath"]), "rb") as wav:
                self.assertEqual((1, 2, 8000, end - start),
                                 (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()))
                self.assertEqual(self.pcm[start * 2:end * 2], wav.readframes(wav.getnframes()))
        self.assertEqual(held, {p: p.read_bytes() for p in held})
        self.assertFalse((output / "INCOMPLETE").exists())

    def test_repeat_outputs_are_byte_identical(self) -> None:
        prepare(self.plan_path, self.root / "one")
        prepare(self.plan_path, self.root / "two")
        for path in (self.root / "one").iterdir():
            self.assertEqual(path.read_bytes(), (self.root / "two" / path.name).read_bytes())

    def test_explicit_author_attribution_travels_with_license_and_modifications(self) -> None:
        self.plan["license"]["attribution"] = "Fixture authors: Example One and Example Two — synthetic only."
        self.save()
        output=self.root/"attributed"
        result=prepare(self.plan_path,output)
        self.assertEqual("attribution.txt",result["retainedAttributionPath"])
        notice_bytes=(output/result["retainedAttributionPath"]).read_bytes()
        self.assertEqual(hashlib.sha256(notice_bytes).hexdigest(),result["retainedAttributionSha256"])
        notice=notice_bytes.decode("utf-8")
        self.assertIn(self.plan["license"]["attribution"],notice)
        self.assertIn(self.plan["license"]["sourceUrl"],notice)
        self.assertIn(result["changeNotice"],notice)
        self.assertIn("does not establish performer rights",notice)
        self.assertEqual(self.license.read_bytes(),(output/result["retainedLicensePath"]).read_bytes())
        self.assertEqual("NOT_APPROVED",result["rightsClearance"])
        self.assertEqual("NOT_APPROVED",result["bankAdmission"])

    def test_attribution_edits_change_notice_binding_without_changing_audio_or_license(self) -> None:
        original=prepare(self.plan_path,self.root/"original-notice")
        self.plan["license"]["attribution"]="Changed fixture author — synthetic only."
        self.save()
        changed=prepare(self.plan_path,self.root/"changed-notice")
        self.assertNotEqual(original["retainedAttributionSha256"],changed["retainedAttributionSha256"])
        self.assertEqual(original["proposals"],changed["proposals"])
        for result,directory in [(original,"original-notice"),(changed,"changed-notice")]:
            notice=self.root/directory/result["retainedAttributionPath"]
            self.assertEqual(hashlib.sha256(notice.read_bytes()).hexdigest(),result["retainedAttributionSha256"])
            self.assertEqual("NOT_APPROVED",result["rightsClearance"])
            self.assertEqual("NOT_APPROVED",result["bankAdmission"])
        self.assertEqual((self.root/"original-notice/license-source.txt").read_bytes(),
                         (self.root/"changed-notice/license-source.txt").read_bytes())

    def test_silent_pause_is_preserved_and_reported_without_acceptance(self) -> None:
        with wave.open(str(self.audio), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(8000)
            wav.writeframes(bytes(1600))
        self.plan["sources"][0]["audio"] = self.binding(self.audio)
        self.plan["spans"] = [{"sourceId": "fixture", "firstLabelIndex": 3, "labelCount": 1}]
        self.save()
        result = prepare(self.plan_path, self.root / "silent")
        self.assertEqual(["pau"], result["proposals"][0]["sourcePhones"])
        self.assertFalse(result["proposals"][0]["nonzeroPcm"])
        self.assertEqual("NOT_APPROVED", result["bankAdmission"])

    def test_rejects_each_stale_file_binding_without_creating_output(self) -> None:
        for path in (self.audio, self.labels, self.license):
            original = path.read_bytes()
            with self.subTest(path=path.name):
                path.write_bytes(original + b" ")
                self.refuse("SHA-256 mismatch")
                path.write_bytes(original)

    def test_rejects_malformed_label_times_columns_and_unicode(self) -> None:
        bad = ["NaN 125 aa", "Inf 125 aa", "-0.1 125 aa", "1e99 125 aa", "0.02 125 aa\n0.01 125 s",
               "0.1001 125 aa", "0.01 256 aa", "0.01 nope aa", "0.01 125", "0.01 125 café",
               "0." + "1" * 100 + " 125 aa"]
        for text in bad:
            with self.subTest(text=text):
                self.labels.write_text(text + "\n")
                self.plan["sources"][0]["labels"] = self.binding(self.labels)
                self.refuse("label|Label|ascii|Expected")

    def test_rejects_zero_frame_span_and_out_of_range_requests(self) -> None:
        self.labels.write_text("#\n0.00000001 125 aa\n0.00000002 125 s\n0.05 125 aa\n")
        self.plan["sources"][0]["labels"] = self.binding(self.labels)
        self.plan["spans"] = [{"sourceId":"fixture", "firstLabelIndex":1, "labelCount":1}]
        self.refuse("empty or invalid PCM extent")
        for first, count in [(99,1), (-1,1), (True,1), (0,True), (0,4), (0,0)]:
            with self.subTest(first=first, count=count):
                self.plan["spans"] = [{"sourceId":"fixture", "firstLabelIndex":first, "labelCount":count}]
                self.refuse("span.*(index|count|exceeds)")

    def test_duplicate_requests_sources_and_unknown_sources_are_refused(self) -> None:
        original = copy.deepcopy(self.plan)
        self.plan["spans"].append(copy.deepcopy(self.plan["spans"][0]))
        self.refuse("Duplicate donor span")
        self.plan = copy.deepcopy(original)
        self.plan["sources"].append(copy.deepcopy(self.plan["sources"][0]))
        self.refuse("duplicate source id")
        self.plan["sources"][1]["id"] = "another"
        self.refuse("Duplicate source audio hash")
        self.plan = copy.deepcopy(original)
        self.plan["spans"][0]["sourceId"] = "absent"
        self.refuse("Unknown span source")

    def test_contract_language_and_injected_approval_are_refused(self) -> None:
        original = copy.deepcopy(self.plan)
        for mutate in [lambda p:p.update(schemaVersion=True), lambda p:p.update(language="ja"),
                       lambda p:p["labelContract"].update(annotationStatus="REVIEWED"),
                       lambda p:p.update(bankAdmission="APPROVED"),
                       lambda p:p["sources"][0].update(phoneticReview="PASS"),
                       lambda p:p["spans"][0].update(assignment="sustain:aa")]:
            self.plan = copy.deepcopy(original)
            mutate(self.plan)
            self.refuse("Unsupported|unknown fields")

    def test_rejects_duplicate_json_and_cli_reports_deep_json_without_traceback(self) -> None:
        self.plan_path.write_text('{"schemaVersion":1,"schemaVersion":1}')
        with self.assertRaisesRegex(ValueError,"Duplicate JSON key"):
            prepare(self.plan_path, self.root / "refused")
        self.plan_path.write_text('{"x":' + '[' * 1500 + '0' + ']' * 1500 + '}')
        script = Path(__file__).resolve().parents[1] / "tools/voicebank_source_intake/prepare_phone_donors.py"
        run = subprocess.run([sys.executable, str(script), "--plan", str(self.plan_path),
                              "--output", str(self.root / "deep")],capture_output=True,text=True,timeout=10)
        self.assertEqual(2, run.returncode)
        self.assertIn("Preparation refused", run.stderr)
        self.assertNotIn("Traceback", run.stderr)
        self.assertFalse((self.root / "deep").exists())

    def test_symlink_and_traversal_sources_and_symlink_plan_are_refused(self) -> None:
        original = copy.deepcopy(self.plan)
        for name in ("../speech.wav", str(self.audio)):
            self.plan["sources"][0]["audio"]["path"] = name
            self.refuse("below the plan directory")
        self.plan = original
        link = self.root / "linked.wav"
        link.symlink_to(self.audio)
        self.plan["sources"][0]["audio"]["path"] = link.name
        self.refuse("Symlink input")
        plan_link = self.root / "linked.json"
        plan_link.symlink_to(self.plan_path)
        with self.assertRaisesRegex(ValueError,"symlink"):
            prepare(plan_link, self.root / "plan-link-output")

    def test_existing_output_and_dangling_symlink_are_preserved(self) -> None:
        output = self.root / "occupied"
        output.mkdir()
        (output / "user.txt").write_text("preserve")
        with self.assertRaisesRegex(ValueError,"no overwrite"):
            prepare(self.plan_path, output)
        self.assertEqual("preserve",(output / "user.txt").read_text())
        link = self.root / "dangling"
        link.symlink_to(self.root / "absent")
        with self.assertRaisesRegex(ValueError,"no overwrite"):
            prepare(self.plan_path, link)
        self.assertTrue(link.is_symlink())

    def test_partial_write_keeps_incomplete_marker(self) -> None:
        output = self.root / "partial"
        with patch("tools.voicebank_source_intake.prepare_phone_donors.wave.open",side_effect=OSError("controlled write")):
            with self.assertRaisesRegex(OSError,"controlled write"):
                prepare(self.plan_path,output)
        self.assertTrue((output / "INCOMPLETE").is_file())
        self.assertFalse((output / "donors.json").exists())

    def test_total_input_and_output_bounds_are_checked_before_writes(self) -> None:
        for setting in ("MAXIMUM_TOTAL_AUDIO_BYTES", "MAXIMUM_TOTAL_DONOR_BYTES"):
            with self.subTest(setting=setting), patch("tools.voicebank_source_intake.prepare_phone_donors."+setting, 100):
                self.refuse("Total.*audio exceeds bounds")

    def test_ancillary_wav_chunk_is_removed_without_pcm_changes(self) -> None:
        raw = self.audio.read_bytes()
        added = raw[:12] + b"JUNK" + struct.pack("<I",1) + b"x\0" + raw[12:]
        added = added[:4] + struct.pack("<I",len(added)-8) + added[8:]
        self.audio.write_bytes(added)
        self.plan["sources"][0]["audio"] = self.binding(self.audio)
        self.save()
        result = prepare(self.plan_path,self.root / "ancillary")
        target = self.root / "ancillary" / result["proposals"][0]["derivedPath"]
        with wave.open(str(target),"rb") as wav:
            self.assertEqual(self.pcm[:200],wav.readframes(wav.getnframes()))
        self.assertNotIn(b"JUNK",target.read_bytes())

    def test_malformed_riff_and_pcm_geometry_are_refused(self) -> None:
        original = self.audio.read_bytes()
        variants=[]
        for offset, value, fmt in [(4,len(original),"<I"),(20,3,"<H"),(22,2,"<H"),(28,16001,"<I"),(32,4,"<H"),(34,24,"<H")]:
            raw=bytearray(original);struct.pack_into(fmt,raw,offset,value);variants.append(bytes(raw))
        variants += [original[:-1], original+b"x", original[:36]+original[12:36]+original[36:]]
        for raw in variants:
            with self.subTest(raw=raw[:44]):
                self.audio.write_bytes(raw)
                self.plan["sources"][0]["audio"] = self.binding(self.audio)
                self.refuse("WAV|PCM|RIFF")

    def test_duplicate_missing_and_unpadded_chunks_hit_specific_refusals(self) -> None:
        original = self.audio.read_bytes()
        fmt, data = original[12:36], original[36:]
        for body, reason in [(fmt+fmt+data, "Repeated or short WAV format"),
                             (fmt+data+data, "Repeated WAV data"),
                             (data, "Missing WAV format or data"),
                             (fmt, "Missing WAV format or data"),
                             (fmt+data+b"1234", "Truncated WAV chunk header"),
                             (fmt+data+b"JUNK"+struct.pack("<I",1)+b"x", "Truncated WAV chunk or padding")]:
            with self.subTest(reason=reason):
                self.audio.write_bytes(b"RIFF"+struct.pack("<I",len(body)+4)+b"WAVE"+body)
                self.plan["sources"][0]["audio"] = self.binding(self.audio)
                self.refuse(reason)


if __name__ == "__main__":
    unittest.main()
