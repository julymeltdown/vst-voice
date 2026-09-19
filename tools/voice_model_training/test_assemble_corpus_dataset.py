"""Dataset configuration assembly must bind real reviews and admit nothing."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import unittest

from tools.voice_model_training.__main__ import encode_report, load_config
from tools.voice_model_training.assemble_corpus_dataset import assemble_corpus_dataset, main
from tools.voice_model_training.permissions import TRAINING_PERMISSIONS
from tools.voice_model_training.test_audio_source import float_wav
from tools.voice_model_training.author_review import author_review


class AssembleCorpusDatasetTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.corpus = self.root / "corpus"
        self.corpus.mkdir()
        self.seed = self.root / "seed.bin"
        self.seed.write_bytes(bytes(range(32)))
        os.chmod(self.seed, 0o600)
        self.build_corpus()

    def build_corpus(self):
        source_id = "corpus-song"
        audio = float_wav([0.25 if i % 2 else -0.25 for i in range(8192)])
        (self.corpus / "song-000").mkdir()
        (self.corpus / "song-000" / "source.wav").write_bytes(audio)
        digest = hashlib.sha256(audio).hexdigest()
        evidence = b"First-party procedural render."
        (self.corpus / "corpus-evidence.txt").write_bytes(evidence)
        permissions = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1,
            sampleRate=48000,
            sources=[dict(sourceId=source_id, songId="song-000", sessionId="session",
                          lineageId="lineage", path="song-000/source.wav", sourceSha256=digest)],
            evidence={"corpus-evidence": "corpus-evidence.txt"},
            manifest=dict(formatId="com.project-seam.training-permission-manifest", schemaVersion=1,
                sources=[dict(sourceId=source_id, sourceSha256=digest, identityId="identity",
                    kind="PROCEDURAL_SYNTHESIS", evidenceId="corpus-evidence",
                    evidenceSha256=hashlib.sha256(evidence).hexdigest(),
                    permissions=dict.fromkeys(TRAINING_PERMISSIONS, True),
                    reviewRevision="operator-authored")]))
        labels = dict(formatId="com.project-seam.voice-training-label-config", schemaVersion=3,
            sampleRate=48000, vocabulary=["a"], minimumConfidence=0.0,
            sources=[dict(sourceId=source_id, songId="song-000", sessionId="session",
                          lineageId="lineage", path="song-000/source.wav", sourceSha256=digest)],
            labels=[dict(sourceSha256=digest, audioSha256=digest,
                label=dict(sourceId=source_id, frameCount=8192, hopSize=256, reviewRevision=None,
                           f0Hz=[220.0] * 32, voiced=[True] * 32,
                           phonemes=[dict(symbol="a", startFrame=0, endFrame=8192, confidence=1.0)]),
                score=dict(language="ja", silencePhones=[],
                           syllables=[dict(lyric="あ", phoneStart=0, phoneEnd=1)],
                           notes=[dict(startFrame=0, endFrame=8192, midi=60, syllable=0, slur=False)]))])
        for name, value in (("permissions.json", permissions), ("labels.json", labels)):
            (self.corpus / name).write_bytes(encode_report(value))
        permissions_sha256 = hashlib.sha256(encode_report(permissions)).hexdigest()
        labels_sha256 = hashlib.sha256(encode_report(labels)).hexdigest()
        corpus = dict(formatId="com.project-seam.captured-teacher-corpus", schemaVersion=1,
            state="PREPARED_UNAPPROVED", seed="dataset-seed", heldOutSongIds=[source_id],
            songs=[dict(sourceId=source_id, sourceSha256=digest)],
            permissionsSha256=permissions_sha256, labelsSha256=labels_sha256,
            sourceRightsAdmitted=False, labelsAdmitted=False, trainingAdmitted=False,
            singerQualified=False, releaseEligible=False)
        (self.corpus / "corpus.json").write_bytes(encode_report(corpus))
        self.receipt = hashlib.sha256(encode_report(corpus)).hexdigest()
        self.rights = author_review(kind="rights", configuration=self.corpus / "permissions.json",
            configuration_sha256=permissions_sha256, seed=self.seed, output=self.root / "rights",
            key_id="owner-key", signer_id="owner", valid_seconds=3600)
        self.label = author_review(kind="label", configuration=self.corpus / "labels.json",
            configuration_sha256=labels_sha256, seed=self.seed, output=self.root / "label",
            key_id="owner-key", signer_id="owner", valid_seconds=3600)

    def assemble(self, **changes):
        arguments = dict(corpus=self.corpus, receipt=self.receipt,
            rights_review=self.root / "rights" / "review.json",
            rights_policy=self.root / "rights" / "policy.json",
            label_review=self.root / "label" / "review.json",
            label_policy=self.root / "label" / "policy.json", output=self.root / "dataset.json")
        arguments.update(changes)
        return assemble_corpus_dataset(**arguments)

    def test_configuration_binds_both_reviews_and_admits_nothing(self):
        result = self.assemble()
        for field in ("sourceRightsAdmitted", "labelsAdmitted", "trainingAdmitted", "releaseEligible"):
            self.assertIs(result[field], False)
        configuration = json.loads((self.root / "dataset.json").read_bytes())
        self.assertEqual(set(configuration), {"formatId", "schemaVersion", "seed", "heldOutSongs",
            "permissionConfig", "labelConfig", "rightsReview", "rightsPolicy", "labelReview", "labelPolicy"})
        self.assertEqual(configuration["heldOutSongs"], ["corpus-song"])
        self.assertEqual(configuration["seed"], "dataset-seed")
        # Every reference must match the bytes physically present in the corpus root.
        for key, reference in configuration.items():
            if not isinstance(reference, dict):
                continue
            payload = (self.corpus / reference["path"]).read_bytes()
            with self.subTest(key=key):
                self.assertEqual(hashlib.sha256(payload).hexdigest(), reference["sha256"])
        # A wrong receipt cannot retarget assembly at different configuration bytes.
        with self.assertRaises(ValueError):
            self.assemble(output=self.root / "bad.json", receipt="0" * 64)

    def test_a_review_of_another_configuration_is_refused(self):
        # Repointing the label review at the permission configuration hash must fail,
        # because a review authorizes exactly the subject it was signed over.
        with self.assertRaises(ValueError):
            self.assemble(output=self.root / "x.json", label_review=self.root / "rights" / "review.json",
                          label_policy=self.root / "rights" / "policy.json")

    def test_missing_or_exposed_artifacts_are_refused(self):
        with self.assertRaises(ValueError):
            self.assemble(output=self.root / "y.json", rights_policy=self.root / "absent.json")
        link = self.root / "linked.json"
        link.symlink_to(self.root / "rights" / "policy.json")
        with self.assertRaises(ValueError):
            self.assemble(output=self.root / "z.json", rights_policy=link)
        for songs in ([], ["absent"], ["corpus-song", "corpus-song"]):
            with self.subTest(songs=songs), self.assertRaises(ValueError):
                self.assemble(output=self.root / f"s{len(songs)}.json", held_out_songs=songs)
        with self.assertRaises(ValueError):
            self.assemble(output=self.root / "seed.json", seed="bad\x01seed")

    def test_an_already_admitted_corpus_and_existing_output_are_refused(self):
        corpus_path = self.corpus / "corpus.json"
        original = corpus_path.read_bytes()
        corpus_path.write_bytes(encode_report({**load_config(corpus_path, self.receipt),
                                               "trainingAdmitted": True}))
        changed_receipt = hashlib.sha256(corpus_path.read_bytes()).hexdigest()
        with self.assertRaises(ValueError):
            self.assemble(output=self.root / "admitted.json", receipt=changed_receipt)
        corpus_path.write_bytes(original)
        self.assemble()
        with self.assertRaises(ValueError): self.assemble()

    def test_cli_writes_the_configuration(self):
        output = self.root / "cli"
        code = main(["--corpus", str(self.corpus), "--receipt", self.receipt,
            "--rights-review", str(self.root / "rights" / "review.json"),
            "--rights-policy", str(self.root / "rights" / "policy.json"),
            "--label-review", str(self.root / "label" / "review.json"),
            "--label-policy", str(self.root / "label" / "policy.json"), "--output", str(output),
            "--held-out-song", "corpus-song"])
        self.assertEqual(code, 0)
        self.assertTrue(output.is_file())
        self.assertEqual(json.loads(output.read_bytes())["heldOutSongs"], ["corpus-song"])


if __name__ == "__main__":
    unittest.main()
