import unittest
import numpy as np

from tools.voice_model_training.periodicity_optimizer_probe import unvoiced_statistics


class OptimizerProbeTests(unittest.TestCase):
    def phones(self):
        return [dict(symbol='s', startFrame=0, endFrame=1024),
                dict(symbol='a', startFrame=1024, endFrame=2048)]

    def test_over_range_output_is_reported_not_rejected(self):
        reference = np.zeros(2048, np.float32)
        candidate = np.full(2048, 3.0, np.float32)
        stats = unvoiced_statistics(reference, candidate, self.phones())
        self.assertTrue(stats['overRange'])
        self.assertAlmostEqual(stats['candidatePeak'], 3.0)
        self.assertEqual(stats['windows'], 1)
        self.assertIsNone(stats['meanLagCorrelation'])  # constant segment -> no correlation

    def test_voiced_regions_are_excluded(self):
        reference = np.zeros(2048, np.float32)
        candidate = np.concatenate([np.ones(1024), np.full(1024, 9.0)]).astype(np.float32)
        stats = unvoiced_statistics(reference, candidate, self.phones())
        self.assertEqual(stats['windows'], 1)
        self.assertAlmostEqual(stats['rows'][0]['candidateRms'], 1.0)

    def test_length_mismatch_is_reported_and_prefix_compared(self):
        stats = unvoiced_statistics(np.zeros(2048), np.ones(1024) * .5,
            [dict(symbol='s', startFrame=0, endFrame=1024)])
        self.assertEqual(stats['lengthMismatch']['candidateSamples'], 1024)
        self.assertEqual(stats['lengthMismatch']['comparedSamples'], 1024)
        self.assertTrue(stats['finite'])

    def test_contiguity_and_finiteness_enforced(self):
        with self.assertRaises(ValueError):
            unvoiced_statistics(np.zeros(2048), np.zeros(2048),
                [dict(symbol='s', startFrame=1, endFrame=2048)])
        diverged = unvoiced_statistics(np.zeros(2048), np.full(2048, np.nan),
            [dict(symbol='s', startFrame=0, endFrame=2048)])
        self.assertFalse(diverged['finite'])
        self.assertEqual(diverged['nonFiniteSamples'], 2048)
        self.assertEqual(diverged['windows'], 0)
        with self.assertRaises(ValueError):
            unvoiced_statistics(np.full(2048, np.nan), np.zeros(2048),
                [dict(symbol='s', startFrame=0, endFrame=2048)])


if __name__ == '__main__':
    unittest.main()
