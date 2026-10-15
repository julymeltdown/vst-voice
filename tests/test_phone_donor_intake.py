from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from unittest.mock import patch

from tools.voicebank_source_intake.prepare_phone_donors import LABEL_CONTRACT, prepare
from tools.voicebank_source_intake import prepare_phone_donors as donor_tool


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

    def native(self, output: Path, expected: str, donor: str, *, success: bool) -> subprocess.CompletedProcess:
        configured=os.environ.get("SEAM_SOURCE_INTAKE_CLI")
        cli=Path(configured) if configured else Path(__file__).resolve().parents[1]/"build/release/seam_voicebank_cli"
        if configured:
            self.assertTrue(cli.is_file(),"Configured native source-intake CLI is missing")
        elif not cli.is_file():
            self.skipTest("Optional direct native CLI unavailable; configured CTest requires it")
        run=subprocess.run([str(cli),"verify-phone-donor",str(output),expected,donor],
                           capture_output=True,text=True,timeout=10)
        self.assertEqual(0 if success else 2,run.returncode,run.stderr)
        if not success:
            self.assertIn("Prepared donor refused",run.stderr)
            self.assertEqual("",run.stdout)
        return run

    def test_native_selected_donor_snapshots_match_prepared_bytes_without_mutation(self) -> None:
        output=self.root/"native-selected"
        result=prepare(self.plan_path,output)
        held={p.name:p.read_bytes() for p in output.iterdir()}
        expected=hashlib.sha256(held["donors.json"]).hexdigest()
        for row in result["proposals"]:
            observed=json.loads(self.native(output,expected,row["id"],success=True).stdout)
            for key,value in {"status":"UNAPPROVED","bankAdmission":"NOT_APPROVED","nativeImport":False,
                              "sourceRightsApproved":False,"wholePacketAudioVerified":False,
                              "donorId":row["id"],"audioSha256":row["derivedSha256"],
                              "pcmSha256":row["pcmSha256"],"frames":row["frames"],
                              "sampleRate":row["sampleRate"],"nonzeroPcm":row["nonzeroPcm"],
                              "packetSha256":expected,"planSha256":result["planSha256"],
                              "licenseSha256":result["license"]["sha256"],
                              "attributionSha256":result["retainedAttributionSha256"]}.items():
                self.assertEqual(value,observed[key],key)
        self.assertEqual(held,{p.name:p.read_bytes() for p in output.iterdir()})
        (output/result["proposals"][-1]["derivedPath"]).unlink()
        self.native(output,expected,result["proposals"][0]["id"],success=True)
        with wave.open(str(self.audio),"wb") as wav:
            wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(8000);wav.writeframes(bytes(1600))
        self.plan["sources"][0]["audio"]=self.binding(self.audio);self.save()
        silent=self.root/"native-silent";prepared=prepare(self.plan_path,silent)
        result=json.loads(self.native(silent,hashlib.sha256((silent/"donors.json").read_bytes()).hexdigest(),
                                      prepared["proposals"][0]["id"],success=True).stdout)
        self.assertFalse(result["nonzeroPcm"])

    def test_native_donor_refuses_changed_inputs_links_incomplete_and_untrusted_digest(self) -> None:
        output=self.root/"native-changed";result=prepare(self.plan_path,output)
        expected=hashlib.sha256((output/"donors.json").read_bytes()).hexdigest()
        donor=result["proposals"][0]["id"]
        for path in [output/"attribution.txt",output/"license-source.txt",output/"intake-plan.json",
                     output/result["proposals"][0]["derivedPath"]]:
            raw=path.read_bytes()
            for mode in ["changed","missing","linked"]:
                with self.subTest(file=path.name,mode=mode):
                    try:
                        if mode=="changed":path.write_bytes(raw+b"changed")
                        else:
                            path.unlink()
                            if mode=="linked":path.symlink_to(self.license)
                        self.native(output,expected,donor,success=False)
                    finally:
                        if path.exists() or path.is_symlink():path.unlink()
                        path.write_bytes(raw)
        for sha in ["","a"*64,"A"*64,"invalid"]:
            with self.subTest(sha=sha):self.native(output,sha,donor,success=False)
        self.native(output,expected,"../unowned",success=False)
        marker=output/"INCOMPLETE"
        marker.write_text("not complete");self.native(output,expected,donor,success=False);marker.unlink()
        marker.symlink_to(output/"missing");self.native(output,expected,donor,success=False);marker.unlink()
        linked=self.root/"native-linked-directory";linked.symlink_to(output,target_is_directory=True)
        self.native(linked,expected,donor,success=False)
        if hasattr(os,"mkfifo"):
            audio=output/result["proposals"][0]["derivedPath"];raw=audio.read_bytes();audio.unlink()
            os.mkfifo(audio);self.native(output,expected,donor,success=False);audio.unlink();audio.write_bytes(raw)

    def test_native_donor_refuses_relabelled_approval_and_inconsistent_selected_provenance(self) -> None:
        output=self.root/"native-relabelled";result=prepare(self.plan_path,output)
        path=output/"donors.json";raw=path.read_bytes();donor=result["proposals"][0]["id"]
        changes=[lambda p:p.update(bankAdmission="APPROVED"),lambda p:p.update(language="ja"),
                 lambda p:p.update(schemaVersion=2),lambda p:p.update(sourceApproved=True),
                 lambda p:p["proposals"][0].update(phoneticReview="REVIEWED"),
                 lambda p:p["proposals"][-1].update(listeningReview="REVIEWED"),
                 lambda p:p["proposals"][0].update(accepted=True),
                 lambda p:p["proposals"][0].update(frames=True),
                 lambda p:p["proposals"][0].update(frames=float(p["proposals"][0]["frames"])),
                 lambda p:p["proposals"][0].update(sampleRate=float(p["proposals"][0]["sampleRate"])),
                 lambda p:p["proposals"][0].update(startFrame=float(p["proposals"][0]["startFrame"])),
                 lambda p:p["proposals"][0].update(endFrameExclusive=float(p["proposals"][0]["endFrameExclusive"])),
                 lambda p:p["proposals"][0].update(nonzeroPcm=1),
                 lambda p:p["proposals"][0].update(durationSeconds="0.01"),
                 lambda p:p["proposals"][0].update(frames=p["proposals"][0]["frames"]+1),
                 lambda p:p["proposals"][0].update(nonzeroPcm=False),
                 lambda p:p["proposals"][0].update(pcmSha256="0"*64),
                 lambda p:p["proposals"][0].update(durationSeconds=999),
                 lambda p:p["proposals"][0].update(sourceType="singing"),
                 lambda p:p["proposals"][0].update(derivedPath="../outside.wav"),
                 lambda p:p["proposals"][0]["sourceAudio"].update(sha256="1"*64),
                 lambda p:p["license"].update(attribution="changed attribution")]
        for i,change in enumerate(changes):
            with self.subTest(change=i):
                changed=json.loads(raw);change(changed);path.write_text(json.dumps(changed))
                self.native(output,hashlib.sha256(path.read_bytes()).hexdigest(),donor,success=False)
        path.write_bytes(b'{"status":"APPROVED",'+raw.lstrip()[1:])
        self.native(output,hashlib.sha256(path.read_bytes()).hexdigest(),donor,success=False)
        path.write_bytes(raw)

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

    def test_packet_verification_and_cli_preserve_unapproved_material(self) -> None:
        output=self.root/"verified"
        result=prepare(self.plan_path,output)
        held={p.name:p.read_bytes() for p in output.iterdir()}
        expected=hashlib.sha256((output/"donors.json").read_bytes()).hexdigest()
        self.assertEqual(result,donor_tool.verify_prepared_phone_donors(output,expected))
        script=Path(donor_tool.__file__)
        run=subprocess.run([sys.executable,str(script),"--verify","--output",str(output),
                            "--expected-donors-sha256",expected],capture_output=True,text=True,timeout=10)
        self.assertEqual(0,run.returncode,run.stderr)
        self.assertIn("Verified 4 unapproved phone donors",run.stdout)
        self.assertEqual(held,{p.name:p.read_bytes() for p in output.iterdir()})

    def test_packet_verification_refuses_changed_missing_linked_and_incomplete_material(self) -> None:
        output=self.root/"tampered"
        result=prepare(self.plan_path,output)
        expected=hashlib.sha256((output/"donors.json").read_bytes()).hexdigest()
        paths=[output/"attribution.txt",output/"license-source.txt",output/"intake-plan.json",
               output/result["proposals"][0]["derivedPath"]]
        for path in paths:
            original=path.read_bytes()
            with self.subTest(path=path.name,mode="changed"):
                path.write_bytes(original+b"changed")
                with self.assertRaises(ValueError):donor_tool.verify_prepared_phone_donors(output,expected)
            path.unlink()
            with self.subTest(path=path.name,mode="missing"):
                with self.assertRaises(ValueError):donor_tool.verify_prepared_phone_donors(output,expected)
            path.symlink_to(self.license)
            with self.subTest(path=path.name,mode="linked"):
                with self.assertRaisesRegex(ValueError,"Symlink"):donor_tool.verify_prepared_phone_donors(output,expected)
            path.unlink();path.write_bytes(original)
        (output/"INCOMPLETE").write_text("unfinished")
        with self.assertRaisesRegex(ValueError,"incomplete"):donor_tool.verify_prepared_phone_donors(output,expected)

    def test_packet_verification_requires_external_digest_and_refuses_relabelled_approval(self) -> None:
        output=self.root/"binding"
        prepare(self.plan_path,output)
        packet=output/"donors.json"
        original=packet.read_bytes()
        for expected in ["", "a"*64, "not-a-hash"]:
            with self.subTest(expected=expected),self.assertRaises(ValueError):
                donor_tool.verify_prepared_phone_donors(output,expected)
        changed=json.loads(original);changed["bankAdmission"]="APPROVED"
        packet.write_text(json.dumps(changed))
        with self.assertRaisesRegex(ValueError,"unapproved"):
            donor_tool.verify_prepared_phone_donors(output,hashlib.sha256(packet.read_bytes()).hexdigest())
        for inject in [lambda p:p.update(sourceApproved=True),
                       lambda p:p["proposals"][0].update(accepted=True)]:
            changed=json.loads(original);inject(changed)
            packet.write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError,"unknown fields"):
                donor_tool.verify_prepared_phone_donors(output,hashlib.sha256(packet.read_bytes()).hexdigest())
        changed=json.loads(original);changed.pop("retainedAttributionSha256")
        packet.write_text(json.dumps(changed))
        with self.assertRaisesRegex(ValueError,"attribution"):
            donor_tool.verify_prepared_phone_donors(output,hashlib.sha256(packet.read_bytes()).hexdigest())

    def test_late_attribution_change_keeps_preparation_incomplete(self) -> None:
        output=self.root/"late-change"
        original_write=Path.write_bytes
        def changed_notice(path,data,*args,**kwargs):
            result=original_write(path,data,*args,**kwargs)
            if path.name=="donors.json":(path.parent/"attribution.txt").write_bytes(b"notice replaced")
            return result
        with patch.object(Path,"write_bytes",changed_notice):
            with self.assertRaisesRegex(ValueError,"SHA-256 mismatch"):
                prepare(self.plan_path,output)
        self.assertTrue((output/"INCOMPLETE").is_file())

    def test_invalid_unicode_provenance_leaves_output_absent_and_allows_corrected_retry(self) -> None:
        original=copy.deepcopy(self.plan)
        output=self.root/"encoding-refusal"
        for key in ["attribution","sourceUrl"]:
            for invalid in [chr(0xd800),chr(0xdfff)]:
                self.plan=copy.deepcopy(original)
                self.plan["license"][key]="invalid-"+invalid
                self.save()
                with self.assertRaises(UnicodeEncodeError):prepare(self.plan_path,output)
                self.assertFalse(output.exists())
        run=subprocess.run([sys.executable,str(Path(donor_tool.__file__)),"--plan",str(self.plan_path),
                            "--output",str(output)],capture_output=True,text=True,timeout=10)
        self.assertEqual(2,run.returncode)
        self.assertIn("Preparation refused",run.stderr)
        self.assertNotIn("Traceback",run.stderr)
        self.assertFalse(output.exists())
        self.plan=original;self.save()
        result=prepare(self.plan_path,output)
        expected=hashlib.sha256((output/"donors.json").read_bytes()).hexdigest()
        self.assertEqual(result,donor_tool.verify_prepared_phone_donors(output,expected))
        self.assertFalse((output/"INCOMPLETE").exists())

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
