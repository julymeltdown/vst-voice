"""A corpus must refuse a split that cannot hold anything out."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.__main__ import encode_report
from tools.voice_model_training.prepare_corpus import load_corpus_config, prepare_corpus
from tools.voice_model_training.test_generated_teacher import captured_pitch, mono_wav


class PrepareCorpusTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.entries = []
        for index, midi in enumerate((60, 64, 67)):
            self.entries.append(self.build_song(index, midi))
        self.value = dict(formatId="com.project-seam.captured-teacher-corpus-config", schemaVersion=1,
            seed="corpus-seed", extractor="unused", songs=self.entries, heldOutSongIds=["song-002"],
            trainingScopes=["sourceUse", "transformation", "modelTraining"])
        self.write_config()

    def build_song(self, index, midi):
        root = self.root / f"export-{index}"
        root.mkdir()
        # Distinct lengths give each song its own audio identity, which is what the
        # split's duplicate-audio guard is entitled to assume of real songs.
        frames = 2048 + 512 * index
        audio = mono_wav(frames)
        recipe = encode_report(dict(song=index))
        recipe_hash = hashlib.sha256(recipe).hexdigest()
        candidate_path = "candidates/0000000000000001-0000000000000002.json"
        # The candidate path encodes the track and region identity, so the authored
        # project must use the same identifiers for the binding to resolve.
        track_id = "1"
        project = dict(formatId="com.project-seam.project", schemaVersion=18, projectId="a",
            vocalTracks=[dict(id=track_id, proceduralRecipe=dict(id="c", version="1", style="neutral",
                contentHash=recipe_hash, path=f"recipes/{recipe_hash}.json"),
            regions=[dict(id="2", lyrics=[dict(id="5", language="ja", surface="さ")],
                          notes=[dict(id="3", lyricId="5", midiKey=midi)])])])
        candidate = dict(formatId="com.project-seam.procedural-candidate", schemaVersion=8,
            approval="unapproved", sampleRate=48000, frameCount=frames,
            audioSha256=hashlib.sha256(audio).hexdigest(), recipeHash=recipe_hash,
            recipeId="c", recipeVersion="1", style="neutral", renderContentHash="d" * 64,
            proceduralRevision=14, markers=[dict(key="0000000000000003:0", phone="s", startFrame=0,
                                                 endFrame=frames // 2),
                                            dict(key="0000000000000003:1", phone="a", startFrame=frames // 2,
                                                 endFrame=frames)])
        files = {"project.seam": encode_report(project), candidate_path: encode_report(candidate),
                 candidate_path.replace(".json", ".wav"): audio, f"recipes/{recipe_hash}.json": recipe}
        rows = []
        for name, payload in files.items():
            path = root / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(payload)
            rows.append(dict(path=name, sha256=hashlib.sha256(payload).hexdigest()))
        receipt = encode_report(dict(schemaVersion=2, state="COMMITTED", sampleRate=48000, projectId="a",
            includesProjectAndRecipes=True, includesProceduralCandidates=True, files=rows))
        (root / "receipt.json").write_bytes(receipt)
        return dict(exportRoot=str(root), receiptSha256=hashlib.sha256(receipt).hexdigest(),
            candidatePath=candidate_path, sourceId=f"song-{index:03d}", songId=f"song-{index:03d}",
            sessionId=f"authoring-session-{index}", lineageId=f"independent-render-{index}")

    def write_config(self):
        data = encode_report(self.value)
        (self.root / "corpus-config.json").write_bytes(data)
        self.digest = hashlib.sha256(data).hexdigest()

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "optional NumPy required")
    def test_corpus_prepares_distinct_songs_and_proves_the_held_out_partition(self):
        output = self.root / "corpus"
        def measured(executable, path):
            payload = path.read_bytes()
            # The native extractor reports the captured WAV's own geometry.
            return captured_pitch(payload, (len(payload) - 44) // 2)
        with patch("tools.voice_model_training.prepare_captured_teacher.extract_pitch",
                   side_effect=measured):
            result = prepare_corpus(config=self.root / "corpus-config.json",
                                    config_sha256=self.digest, output=output)
        self.assertEqual(result["state"], "PREPARED_UNAPPROVED")
        self.assertEqual(len(result["songs"]), 3)
        # Every song carries its own audio identity, so the corpus has three
        # distinct sources rather than one file measured three times.
        self.assertEqual(result["distinctAudioCount"], 3)
        self.assertEqual(result["split"]["counts"]["test"], 1)
        held = [song for song in result["songs"] if song["heldOutRequested"]]
        self.assertEqual([song["sourceId"] for song in held], ["song-002"])
        self.assertEqual(held[0]["partition"], "test")
        for field in ("sourceRightsAdmitted", "labelsAdmitted", "trainingAdmitted", "singerQualified",
                      "releaseEligible"):
            self.assertIs(result[field], False)
        self.assertEqual(result["totalAnalysisFrames"], sum((2048 + 512 * i + 255) // 256 for i in range(3)))
        for index in range(3):
            self.assertTrue((output / f"song-{index:03d}" / "preparation.json").is_file())
        # The merged label configuration must pass the project's own label inspection,
        # which re-inspects every WAV and re-derives each label from its export.
        from tools.voice_model_training.__main__ import inspect_label_config
        inspection = inspect_label_config(output / "labels.json", result["labelsSha256"], output)
        self.assertEqual(inspection["sources"].__len__(), 3)
        self.assertEqual(len(result["vocabulary"]), len(set(result["vocabulary"])))
        self.assertIn("s", result["vocabulary"])
        self.assertEqual({item["sourceId"] for item in inspection["sources"]},
                         {"song-000", "song-001", "song-002"})
        for entry in inspection["sources"]:
            # Preparation cannot report clean labels: no reviewer has signed these
            # yet, so the only queued correction must be that missing revision.
            self.assertFalse(entry["consistencyPassed"])
            self.assertEqual([issue["code"] for issue in entry["correctionQueue"]],
                             ["review-revision-missing"])
        self.assertFalse(inspection["trainingAdmitted"])
        self.assertFalse(inspection["releaseEligible"])
        # The permission capture must join to the actual WAVs, and it must report the
        # declared scopes rather than an assumed complete set.
        from tools.voice_model_training.__main__ import inspect_permission_config
        permissions = inspect_permission_config(output / "permissions.json",
                                                result["permissionsSha256"], output)
        self.assertTrue(permissions["sourceBytesVerified"])
        self.assertEqual(len(permissions["sources"]), 3)
        self.assertEqual(result["trainingScopes"], ["modelTraining", "sourceUse", "transformation"])
        self.assertIs(result["assertionsComplete"], False)
        for source in permissions["sources"]:
            self.assertEqual(source["missingScopes"],
                             ["singingBankRedistribution", "commercialRenders", "modelRedistribution",
                              "commercialModels"])
        self.assertFalse(permissions["assertionsComplete"])
        self.assertFalse(permissions["trainingAdmitted"])
        self.assertFalse(permissions["reviewAuthenticated"])
        # The merged target inventory is what training actually reads, so it must
        # load through the real loader and bind each matrix by digest.
        from tools.voice_model_training.train import load_targets
        targets, profile = load_targets(output / "targets.json", result["targetsSha256"])
        self.assertEqual(len(targets), 3)
        self.assertEqual({identity for identity in targets},
                         {"song-000", "song-001", "song-002"})
        self.assertEqual(result["targetCount"], 3)
        for identity, (record, path) in targets.items():
            with self.subTest(identity=identity):
                self.assertEqual(record["profileSha256"], profile)
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                                 next(song["targetSha256"] for song in result["songs"]
                                      if song["sourceId"] == identity))
        with self.assertRaises(ValueError): prepare_corpus(config=self.root / "corpus-config.json",
                                                          config_sha256=self.digest, output=output)

    def test_single_song_corpus_and_full_holdout_are_refused(self):
        for change in (dict(songs=self.entries[:1]), dict(heldOutSongIds=[f"song-{i:03d}" for i in range(3)]),
                       dict(heldOutSongIds=["absent-song"]), dict(heldOutSongIds=[]),
                       dict(schemaVersion=2), dict(heldOutSongIds=["song-002", "song-002"])):
            self.value = {**self.value, **change}
            self.write_config()
            with self.subTest(change=change), self.assertRaises(ValueError):
                load_corpus_config(self.root / "corpus-config.json", self.digest)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "optional NumPy required")
    def test_corpus_without_a_trainable_source_is_refused(self):
        # Holding out two of three songs leaves one training source; holding out
        # every song leaves none. Only the second must be refused outright.
        self.value = {**self.value, "heldOutSongIds": ["song-001", "song-002"]}
        self.write_config()
        def measured(executable, path):
            payload = path.read_bytes()
            return captured_pitch(payload, (len(payload) - 44) // 2)
        with patch("tools.voice_model_training.prepare_captured_teacher.extract_pitch", side_effect=measured):
            result = prepare_corpus(config=self.root / "corpus-config.json", config_sha256=self.digest,
                                    output=self.root / "two-held")
        self.assertEqual(result["split"]["counts"]["test"], 2)
        self.assertEqual(result["split"]["counts"]["train"], 1)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "optional NumPy required")
    def test_one_shared_session_merges_songs_and_is_refused(self):
        # Songs that claim one authoring session are one connected source as far as
        # the split is concerned; holding one out then leaves nothing to train on.
        self.value = {**self.value, "songs": [
            {**self.entries[0], "sessionId": "shared-session"},
            {**self.entries[1], "sessionId": "shared-session"},
            {**self.entries[2], "sessionId": "shared-session"}]}
        self.write_config()
        def measured(executable, path):
            payload = path.read_bytes()
            return captured_pitch(payload, (len(payload) - 44) // 2)
        with patch("tools.voice_model_training.prepare_captured_teacher.extract_pitch", side_effect=measured):
            with self.assertRaisesRegex(ValueError, "no train source"):
                prepare_corpus(config=self.root / "corpus-config.json", config_sha256=self.digest,
                               output=self.root / "merged")
        self.assertFalse((self.root / "merged" / "corpus.json").exists())

    def test_duplicate_song_identity_and_entry_fields_are_refused(self):
        duplicate = copy.deepcopy(self.value)
        duplicate["songs"][1]["songId"] = duplicate["songs"][0]["songId"]
        duplicate["heldOutSongIds"] = ["song-002"]
        self.value = duplicate
        self.write_config()
        with patch("tools.voice_model_training.prepare_captured_teacher.extract_pitch") as extract:
            with self.assertRaises(ValueError):
                prepare_corpus(config=self.root / "corpus-config.json", config_sha256=self.digest,
                               output=self.root / "duplicate")
            extract.assert_not_called()
        malformed = copy.deepcopy(self.value)
        malformed["songs"][0]["extra"] = "field"
        self.value = malformed
        self.write_config()
        with self.assertRaises(ValueError):
            load_corpus_config(self.root / "corpus-config.json", self.digest)

    def test_unbound_config_hash_and_noncanonical_candidate_are_refused(self):
        with self.assertRaises(ValueError):
            load_corpus_config(self.root / "corpus-config.json", "0" * 64)
        for candidate in ("../c.json", "candidates/1-2.wav", "candidates/1-2.jsonx"):
            self.value = {**self.value, "songs": [{**self.entries[0], "candidatePath": candidate},
                                                  self.entries[1], self.entries[2]]}
            self.write_config()
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                load_corpus_config(self.root / "corpus-config.json", self.digest)

    def test_unknown_duplicate_or_missing_training_scopes_are_refused(self):
        for scopes in (["sourceUse", "modelTraining", "modelTraining"], ["sourceUse", "inventedScope"],
                       [], "sourceUse", ["sourceUse", None]):
            self.value = {**self.value, "trainingScopes": scopes}
            self.write_config()
            with self.subTest(scopes=scopes), self.assertRaises(ValueError):
                load_corpus_config(self.root / "corpus-config.json", self.digest)
        # Omitting the declaration entirely is a refusal, never a default scope set.
        self.value = {key: item for key, item in self.value.items() if key != "trainingScopes"}
        self.write_config()
        with self.assertRaises(ValueError):
            load_corpus_config(self.root / "corpus-config.json", self.digest)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "optional NumPy required")
    def test_failure_in_one_song_publishes_no_corpus_receipt(self):
        output = self.root / "partial"
        calls = {"count": 0}
        def flaky(executable, path):
            calls["count"] += 1
            if calls["count"] == 2:
                raise ValueError("native extraction failed")
            payload = path.read_bytes()
            return captured_pitch(payload, (len(payload) - 44) // 2)
        with patch("tools.voice_model_training.prepare_captured_teacher.extract_pitch", side_effect=flaky):
            with self.assertRaises(ValueError):
                prepare_corpus(config=self.root / "corpus-config.json", config_sha256=self.digest,
                               output=output)
        self.assertFalse((output / "corpus.json").exists())
        self.assertTrue((output / "song-000" / "preparation.json").is_file())
        self.assertFalse((output / "song-001" / "preparation.json").exists())


if __name__ == "__main__":
    unittest.main()
