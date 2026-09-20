import unittest
import numpy as np

from tools.voice_model_training.flatness_distribution import compare, exceedance, percentiles


class FlatnessDistributionTests(unittest.TestCase):
    def test_percentiles_and_exceedance(self):
        values = np.linspace(0, 1, 101)
        report = percentiles(values, (50, 100))
        self.assertAlmostEqual(report['p50'], 0.5)
        # 101 samples, strict > 0.9 leaves the 10 values above 0.9.
        self.assertAlmostEqual(exceedance(values, 0.9)['fraction'], 10 / 101)
        self.assertEqual(exceedance(values, 1.0)['count'], 0)   # strict comparison

    def test_never_flat_candidate_is_visible(self):
        reference = np.full(64, 0.8)
        candidate = np.full(64, 0.01)
        report = compare(reference, candidate, thresholds=(0.1, 0.5))
        self.assertEqual(report['candidate']['exceedance'][0]['count'], 0)
        self.assertEqual(report['reference']['exceedance'][1]['fraction'], 1.0)
        self.assertEqual(report['frameCount'], 64)

    def test_invalid_inputs(self):
        with self.assertRaises(ValueError):
            percentiles(np.array([]))
        with self.assertRaises(ValueError):
            percentiles(np.full(4, np.nan))
        with self.assertRaises(ValueError):
            exceedance(np.ones(4), float('nan'))
        with self.assertRaises(ValueError):
            compare(np.ones(4), np.ones(5))


if __name__ == '__main__':
    unittest.main()
