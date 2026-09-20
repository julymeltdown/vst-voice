import unittest

from tools.voice_model_training.summarize_frozen_campaign import aggregate


class FrozenSummaryTests(unittest.TestCase):
    def row(self, cents, pairs, rests):
        return dict(execution='PASSED', metrics=dict(meanAbsoluteCents=cents,
            measurableVoicedPairs=pairs, withinToleranceFrames=pairs-1,
            unmeasurableFrames=2, voicingMismatchFrames=3), score=dict(
            errorLocations=dict(counts=dict(mismatches=1, noteInterior=1)),
            silence=dict(rests=rests)))

    def test_weighted_summary_and_channel_silence(self):
        rows = [self.row(10, 10, []), self.row(20, 30, [dict(channelNonzeroSamples=[0, 0])])]
        result = aggregate(rows)
        self.assertEqual(result['weightedMeanAbsoluteCents'], 17.5)
        self.assertEqual(result['withinToleranceFrames'], 38)
        self.assertEqual(result['errorLocations']['mismatches'], 2)
        self.assertTrue(result['allRestsExactlyZero'])
        rows[1]['score']['silence']['rests'][0]['channelNonzeroSamples'][1] = 1
        self.assertFalse(aggregate(rows)['allRestsExactlyZero'])

    def test_failures_and_unmeasurable_rows_prevent_aggregate(self):
        self.assertIsNone(aggregate([]))
        self.assertIsNone(aggregate([self.row(10, 10, []), dict(execution='FAILED')]))
        self.assertIsNone(aggregate([self.row(None, 0, [])]))
        self.assertIsNone(aggregate([self.row(10, 10, [])])['allRestsExactlyZero'])


if __name__ == '__main__':
    unittest.main()
