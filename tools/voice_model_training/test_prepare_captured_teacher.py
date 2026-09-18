"""Receipt binding and no-approval guarantees of captured teacher preparation."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.__main__ import encode_report
from tools.voice_model_training.prepare_captured_teacher import capture_inputs, prepare_bundle
from tools.voice_model_training.test_generated_teacher import captured_pitch, mono_wav


class PrepareBundleTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.audio = mono_wav(2048)
        self.path = "candidates/0000000000000001-0000000000000002.json"
        self.recipe = encode_report(dict(originalTestRecipe=True))
        self.recipe_hash = hashlib.sha256(self.recipe).hexdigest()
        self.recipe_ref = dict(id="test", version="1", style="neutral", contentHash=self.recipe_hash,
                               path=f"recipes/{self.recipe_hash}.json")
        self.project = dict(formatId="com.project-seam.project", schemaVersion=18, projectId="a",
            vocalTracks=[dict(id="1", proceduralRecipe=self.recipe_ref,
            regions=[dict(id="2", notes=[dict(id="3", lyricId="5", midiKey=60),
                                        dict(id="4", lyricId="6", midiKey=62)],
            lyrics=[dict(id="5", language="ja", surface="さ"), dict(id="6", language="ja", surface="ー")])])])
        self.candidate = dict(formatId="com.project-seam.procedural-candidate", schemaVersion=8,
            approval="unapproved", sampleRate=48000, frameCount=2048,
            audioSha256=hashlib.sha256(self.audio).hexdigest(), recipeHash=self.recipe_hash,
            recipeId="test", recipeVersion="1", style="neutral", renderContentHash="c" * 64,
            proceduralRevision=14, markers=[
                dict(key="0000000000000003:0", phone="s", startFrame=0, endFrame=256),
                dict(key="0000000000000003:1", phone="a", startFrame=256, endFrame=1024),
                dict(key="0000000000000004:0", phone="a", startFrame=1024, endFrame=2048)])
        self.write_export()

    def write_export(self):
        files = {"project.seam": encode_report(self.project), self.path: encode_report(self.candidate),
                 self.path.replace(".json", ".wav"): self.audio, self.recipe_ref["path"]: self.recipe}
        rows = []
        for name, payload in files.items():
            path = self.root / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(payload)
            rows.append(dict(path=name, sha256=hashlib.sha256(payload).hexdigest()))
        self.receipt = dict(schemaVersion=2, state="COMMITTED", sampleRate=48000, projectId="a",
            includesProjectAndRecipes=True, includesProceduralCandidates=True, files=rows)
        self.write_receipt()

    def write_receipt(self):
        data = encode_report(self.receipt)
        (self.root / "receipt.json").write_bytes(data)
        self.digest = hashlib.sha256(data).hexdigest()

    def capture(self):
        return capture_inputs(self.root, self.digest, self.path)

    def test_receipt_bound_notes_are_grouped_not_duplicated_per_phone(self):
        _, payload, lyrics, midi, provenance = self.capture()
        self.assertEqual(payload, self.audio)
        self.assertEqual(lyrics, ["さ", "ー"])
        self.assertEqual(midi, [60, 62])
        self.assertEqual(provenance["exportReceiptSha256"], self.digest)

    def test_mutated_project_or_wav_or_metadata_is_rejected(self):
        for name in ("project.seam", self.path, self.path.replace(".json", ".wav")):
            with self.subTest(name=name):
                file = self.root / name
                original = file.read_bytes()
                file.write_bytes(original + b" ")
                with self.assertRaises(ValueError): self.capture()
                file.write_bytes(original)

    def test_duplicate_receipt_entry_and_uncommitted_export_are_rejected(self):
        original = copy.deepcopy(self.receipt)
        for change in (dict(files=self.receipt["files"] * 2), dict(state="STAGING"),
                       dict(sampleRate=44100), dict(schemaVersion=True)):
            self.receipt = {**original, **change}
            self.write_receipt()
            with self.subTest(change=change), self.assertRaises(ValueError): self.capture()

    def test_missing_note_non_japanese_and_duplicate_project_ids_are_rejected(self):
        region = self.project["vocalTracks"][0]["regions"][0]
        original = copy.deepcopy(region)
        for change in (dict(notes=original["notes"][:1]), dict(notes=original["notes"] * 2),
                       dict(lyrics=[dict(id="5", language="en", surface="sa"), original["lyrics"][1]])):
            region.clear()
            region.update({**original, **change})
            self.write_export()
            with self.subTest(change=change), self.assertRaises(ValueError): self.capture()

    def test_recipe_mismatch_and_missing_bound_recipe_rejected(self):
        self.candidate["recipeHash"] = "0" * 64
        self.write_export()
        with self.assertRaises(ValueError): self.capture()
        self.candidate["recipeHash"] = self.recipe_hash
        self.write_export()
        self.receipt["files"].pop()
        self.write_receipt()
        with self.assertRaises(ValueError): self.capture()

    def test_symlink_and_noncanonical_candidate_rejected(self):
        source = self.root / self.path.replace(".json", ".wav")
        source.unlink()
        source.symlink_to(self.root / "project.seam")
        with self.assertRaises(ValueError): self.capture()
        for name in ("../candidate.json", "candidates/1-2.json", "/tmp/candidate.json"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                capture_inputs(self.root, self.digest, name)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "optional NumPy required")
    def test_complete_bundle_loads_targets_without_admitting_or_splitting(self):
        output = self.root / "prepared"
        arguments = dict(export_root=self.root, receipt_sha256=self.digest, candidate_path=self.path,
                         extractor=Path("unused"), output=output, source_id="test", song_id="song",
                         session_id="session", lineage_id="teacher")
        with patch("tools.voice_model_training.prepare_captured_teacher.extract_pitch",
                   return_value=captured_pitch(self.audio, 2048)):
            result = prepare_bundle(**arguments)
        self.assertEqual(result["state"], "PREPARED_UNAPPROVED")
        self.assertEqual(result["analysisFrameCount"], 8)
        self.assertEqual((result["phoneCount"], result["noteCount"], result["slurCount"]), (3, 2, 1))
        for field in ("independentSplitCreated", "sourceRightsAdmitted", "labelsAdmitted",
                      "trainingAdmitted", "releaseEligible"):
            self.assertIs(result[field], False)
        from tools.voice_model_training.train import load_targets
        targets, _ = load_targets(output / "targets.json", result["artifacts"]["targets.json"])
        self.assertEqual(targets["test"][0]["analysisFrameCount"], 8)
        self.assertEqual(hashlib.sha256(targets["test"][1].read_bytes()).hexdigest(), result["targetSha256"])
        with self.assertRaises(ValueError): prepare_bundle(**arguments)

    def test_extractor_failure_does_not_publish_complete_receipt(self):
        output = self.root / "failed"
        with patch("tools.voice_model_training.prepare_captured_teacher.extract_pitch", side_effect=ValueError("failed")):
            with self.assertRaises(ValueError):
                prepare_bundle(export_root=self.root, receipt_sha256=self.digest, candidate_path=self.path,
                    extractor=Path("unused"), output=output, source_id="test", song_id="song",
                    session_id="session", lineage_id="teacher")
        self.assertTrue((output / "source.wav").exists())
        self.assertFalse((output / "preparation.json").exists())


if __name__ == "__main__":
    unittest.main()
