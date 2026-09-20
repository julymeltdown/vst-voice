import copy
import hashlib
import json
import unittest

from tools.voice_model_training.__main__ import encode_report
from tools.voice_model_training.split import split_sources
from tools.voice_model_training.training_ancestry import overlaps, trace, training_inventory


def seal(receipt):
    receipt["metadataSha256"] = hashlib.sha256(json.dumps(
        dict(metadata=receipt["metadata"], epoch=receipt["epoch"]), sort_keys=True,
        ensure_ascii=False, separators=(",", ":")).encode()).hexdigest()
    return receipt


def fixture():
    rows = [dict(sourceId=s, songId=s, sessionId=s, lineageId=s, audioSha256=d * 64)
            for s, d in (("one", "a"), ("two", "b"), ("held", "c"))]
    split = next(value for i in range(100) if (value := split_sources(rows, seed=str(i),
        held_out_songs=["held"]))["counts"]["train"] > 0)
    bindings = dict(split=split)
    dataset = hashlib.sha256(encode_report(bindings)).hexdigest()
    snapshot = dict(formatId="com.project-seam.training-dataset-snapshot", schemaVersion=3,
                    sources=rows, bindings=bindings, datasetSha256=dataset)
    trained = {s for group in split["groups"] if group["partition"] == "train" for s in group["sourceIds"]}
    receipt = seal(dict(formatId="com.project-seam.training-checkpoint", schemaVersion=1,
        checkpointSha256="7" * 64,
        metadata=dict(datasetBindings=bindings, datasetSha256=dataset,
                      run=dict(completedEpochs=1, parentReceiptSha256=None,
                               trainingConfigurationSha256="6" * 64)),
        epoch=dict(epochComplete=True, coverageVerified=True, datasetSha256=dataset,
                   coveredSourceFrames={s: 20 for s in trained})))
    return receipt, snapshot


class AncestryTests(unittest.TestCase):
    def test_complete_receipt_chain_and_snapshot_inventory(self):
        first, snapshot = fixture()
        second = copy.deepcopy(first)
        second["metadata"]["run"].update(completedEpochs=2, parentReceiptSha256="1" * 64)
        seal(second)
        chain = trace("2" * 64, {"1" * 64: first, "2" * 64: second}.__getitem__)
        self.assertEqual(len(chain), 2)
        records = training_inventory(chain, {snapshot["datasetSha256"]: snapshot})
        self.assertEqual(len(records), len(first["epoch"]["coveredSourceFrames"]))
        self.assertNotIn("held", {row["sourceId"] for row in records})

    def test_broken_missing_and_cyclic_ancestry_refused(self):
        first, _ = fixture()
        for number, parent in ((2, None), (1, "1" * 64)):
            altered = copy.deepcopy(first)
            altered["metadata"]["run"].update(completedEpochs=number, parentReceiptSha256=parent)
            seal(altered)
            with self.assertRaises(ValueError):
                trace("1" * 64, lambda _: altered)
        with self.assertRaises(KeyError):
            trace("1" * 64, {}.__getitem__)

    def test_metadata_tampering_incomplete_and_epoch_gap_refused(self):
        first, _ = fixture()
        altered = copy.deepcopy(first)
        altered["epoch"]["epochComplete"] = False
        with self.assertRaises(ValueError):
            trace("1" * 64, lambda _: altered)
        seal(altered)
        with self.assertRaises(ValueError):
            trace("1" * 64, lambda _: altered)
        altered = copy.deepcopy(first)
        altered["metadata"]["run"].update(completedEpochs=3, parentReceiptSha256="1" * 64)
        seal(altered)
        with self.assertRaises(ValueError):
            trace("2" * 64, {"1" * 64: first, "2" * 64: altered}.__getitem__)

    def test_warm_start_link_is_traced_not_treated_as_new_root(self):
        first, _ = fixture()
        warm = copy.deepcopy(first)
        warm["metadata"]["run"].update(parentReceiptSha256="1" * 64,
            trainingConfigurationSha256="8" * 64, warmStart=dict(sourceReceiptSha256="1" * 64,
                sourceCompletedEpochs=1, sourceCheckpointSha256="7" * 64,
                sourceTrainingConfigurationSha256="6" * 64, optimizerReset=True, rngReset=True))
        seal(warm)
        resolver = {"1" * 64: first, "2" * 64: warm}.__getitem__
        self.assertEqual(len(trace("2" * 64, resolver)), 2)
        warm["metadata"]["run"]["warmStart"]["sourceCheckpointSha256"] = "9" * 64
        seal(warm)
        with self.assertRaises(ValueError):
            trace("2" * 64, resolver)

    def test_snapshot_audio_identity_and_coverage_changes_refused(self):
        receipt, snapshot = fixture()
        chain = trace("1" * 64, lambda _: receipt)
        with self.assertRaises(ValueError):
            training_inventory(chain, {})
        altered = copy.deepcopy(snapshot)
        altered["sources"][0]["audioSha256"] = "d" * 64
        with self.assertRaises(ValueError):
            training_inventory(chain, {snapshot["datasetSha256"]: altered})
        receipt["epoch"]["coveredSourceFrames"] = {}
        seal(receipt)
        with self.assertRaises(ValueError):
            training_inventory(trace("1" * 64, lambda _: receipt), {snapshot["datasetSha256"]: snapshot})

    def test_alias_audio_and_lineage_overlap_are_visible(self):
        receipt, snapshot = fixture()
        records = training_inventory(trace("1" * 64, lambda _: receipt), {snapshot["datasetSha256"]: snapshot})
        original = records[0]
        candidate = dict(sourceId="new", songId="new", sessionId="new", lineageId="new", audioSha256=original["audioSha256"])
        result = overlaps([candidate], records)[0]
        self.assertEqual(result["status"], "TRAINING_OVERLAP")
        self.assertEqual(result["overlaps"][0]["fields"], ["audioSha256"])
        candidate.update(audioSha256="f" * 64, lineageId=original["lineageId"])
        self.assertEqual(overlaps([candidate], records)[0]["status"], "TRAINING_OVERLAP")
        candidate["lineageId"] = "new"
        self.assertEqual(overlaps([candidate], records)[0]["status"], "NO_OVERLAP_IN_DECLARED_FIELDS")
        with self.assertRaises(ValueError):
            overlaps([candidate, candidate], records)


if __name__ == "__main__":
    unittest.main()
