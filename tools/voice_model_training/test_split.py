import unittest
from tools.voice_model_training.split import split_sources


def source(index, **changes):
    row = dict(sourceId=f"source-{index}", songId=f"song-{index}", sessionId=f"session-{index}",
               lineageId=f"lineage-{index}", audioSha256=f"{index:064x}")
    row.update(changes)
    return row


class SplitTests(unittest.TestCase):
    def split(self, records):
        return split_sources(records, seed="pilot-1", held_out_songs=["song-0"])

    def test_deterministic_order_and_all_partitions(self):
        records = [source(index) for index in range(200)]
        result = self.split(records)
        self.assertEqual(result, self.split(list(reversed(records))))
        self.assertEqual(result["missingPartitions"], [])
        self.assertEqual(sum(result["counts"].values()), 200)
        self.assertFalse(result["releaseEligible"])

    def test_transitive_leakage_groups_move_to_explicit_holdout(self):
        records = [source(0), source(1, songId="song-0"), source(2, sessionId="session-1"),
                   source(3, lineageId="lineage-2"), source(4, audioSha256=f"{3:064x}")]
        result = self.split(records)
        self.assertEqual(len(result["groups"]), 1)
        self.assertEqual(result["groups"][0]["partition"], "test")
        self.assertTrue(result["groups"][0]["explicitHeldOut"])
        self.assertEqual(result["missingPartitions"], ["train", "validation"])

    def test_invalid_identity_and_unknown_holdout(self):
        for rows in ([source(0), source(0)], [source(0, audioSha256="A" * 64)],
                     [source(0, lineageId="")], [source(1)]):
            with self.assertRaises(ValueError):
                self.split(rows)
        with self.assertRaises(ValueError):
            split_sources([source(0)], seed="pilot", held_out_songs=[])

    def test_does_not_mutate_sources(self):
        row = source(0)
        before = dict(row)
        self.split([row])
        self.assertEqual(row, before)

    def test_duplicate_dossier_counts_unique_audio_without_selecting_a_copy(self):
        records = [source(0), source(1, audioSha256=f"{0:064x}"), source(2, audioSha256=f"{0:064x}")]
        result = self.split(records)
        self.assertEqual(result["schemaVersion"], 2)
        self.assertEqual(result["counts"]["test"], 3)
        self.assertEqual(result["uniqueAudioCounts"]["test"], 1)
        self.assertEqual(result["redundantSourceCount"], 2)
        self.assertEqual(result["duplicateAudioGroups"], [dict(audioSha256=f"{0:064x}",
            sourceIds=["source-0", "source-1", "source-2"], partition="test")])
        self.assertEqual(result, self.split(records[::-1]))
        self.assertEqual(len(records), 3)


if __name__ == "__main__":
    unittest.main()
