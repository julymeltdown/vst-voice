import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.voice_model_training.__main__ import publish_new
from tools.voice_model_training.combine_corpus_sources import combine
from tools.voice_model_training.prepare_corpus import load_corpus_config


class CombineTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def corpus(self, name, *, identity=None, scope="modelTraining", partition="validation"):
        base = identity or name
        songs = [dict(exportRoot="/captured/" + base, receiptSha256="a" * 64,
                      candidatePath="candidates/0000000000000001-0000000000000002.json",
                      **{field: base + str(i) for field in ("sourceId", "songId", "sessionId", "lineageId")})
                 for i in range(3)]
        config = dict(formatId="com.project-seam.captured-teacher-corpus-config", schemaVersion=1,
                      seed=name, extractor="/trusted/native-cli", songs=songs,
                      heldOutSongIds=[base + "2"], trainingScopes=[scope])
        cp, rp = self.root / (name + "-config.json"), self.root / (name + "-receipt.json")
        publish_new(cp, config)
        digest = hashlib.sha256(cp.read_bytes()).hexdigest()
        publish_new(rp, dict(formatId="com.project-seam.captured-teacher-corpus", state="PREPARED_UNAPPROVED",
                            configurationSha256=digest, songs=[dict(sourceId=base + str(i), partition=p)
                                for i, p in enumerate(("train", partition, "test"))]))
        return cp, digest, rp, hashlib.sha256(rp.read_bytes()).hexdigest()

    def test_preserves_identities_and_protects_prior_validation_and_test(self):
        inputs = [self.corpus("a"), self.corpus("b")]
        originals = [entry[0].read_bytes() for entry in inputs]
        output = self.root / "combined.json"
        report = combine(inputs, seed="fresh", output=output)
        value = load_corpus_config(output, report['sourcesSha256'])
        self.assertEqual(value['heldOutSongIds'], ['a1', 'a2', 'b1', 'b2'])
        self.assertEqual(report['songs'], 6)
        self.assertTrue(report['freshReviewsRequired'])
        self.assertFalse(report['trainingAdmitted'])
        self.assertEqual(originals, [entry[0].read_bytes() for entry in inputs])
        with self.assertRaises(ValueError): combine(inputs, seed="fresh", output=output)

    def test_refuses_scope_expansion_collision_unknown_partition_and_stale_digest(self):
        first = self.corpus("a")
        bad_inputs = [self.corpus("scope", scope="commercialModels"),
                      self.corpus("collision", identity="a"), self.corpus("partition", partition="unknown")]
        stale = list(self.corpus("stale")); stale[1] = '0' * 64; bad_inputs.append(stale)
        for index, other in enumerate(bad_inputs):
            output = self.root / (str(index) + '.json')
            with self.assertRaises(ValueError): combine([first, other], seed="fresh", output=output)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
