import unittest
import numpy as np

from tools.voice_model_training.acoustic_error_decomposition import decompose, error_share


PHONES2 = [dict(symbol='s', startFrame=0, endFrame=4096),
           dict(symbol='a', startFrame=4096, endFrame=8192)]


PHONES = [dict(symbol='s', startFrame=0, endFrame=8192)]


class DecompositionTests(unittest.TestCase):
    def test_pure_offset_is_all_bias(self):
        rng = np.random.default_rng(11)
        target = rng.normal(-8, 1, (32, 8))
        rows, _ = decompose(target, target + 3.0, PHONES)
        row = rows[0]
        self.assertAlmostEqual(row['biasMagnitude'], 3.0, places=6)
        self.assertAlmostEqual(row['structuredError'], 0.0, places=6)
        self.assertAlmostEqual(row['biasConcentration'], 1.0, places=6)
        self.assertAlmostEqual(row['biasExplainedFraction'], 1.0, places=6)

    def test_zero_mean_noise_is_all_structure(self):
        rng = np.random.default_rng(13)
        target = np.zeros((32, 8))
        predicted = rng.normal(0, 1, (32, 8))
        row = decompose(target, predicted, PHONES)[0][0]
        # The per-bin mean of zero-mean noise is small relative to the error.
        self.assertLess(abs(row['biasConcentration']), .3)
        self.assertGreater(row['structuredError'], .5)

    def test_perfect_prediction(self):
        target = np.ones((32, 8))
        row = decompose(target, target, PHONES)[0][0]
        self.assertEqual(row['meanAbsoluteError'], 0.0)
        self.assertIsNone(row['biasExplainedFraction'])  # no error to explain

    def test_short_interval_and_geometry(self):
        rows, _ = decompose(np.ones((8, 8)), np.ones((8, 8)),
                            [dict(symbol='s', startFrame=0, endFrame=256)])
        self.assertEqual(rows[0]['measurements'], 'TOO_FEW_FRAMES')
        with self.assertRaises(ValueError):
            decompose(np.ones((8, 8)), np.ones((8, 8)),
                      [dict(symbol='s', startFrame=1, endFrame=8192)])
        with self.assertRaises(ValueError):
            decompose(np.ones((8, 8)), np.full((8, 8), np.nan), PHONES)

    def test_error_share_separates_frame_share_from_error_contribution(self):
        target = np.zeros((32, 8))
        predicted = np.zeros((32, 8))
        predicted[:16] = 4.0          # unvoiced half is badly wrong
        report = error_share(target, predicted, PHONES2, ('s',))
        self.assertAlmostEqual(report['frameShare'], 0.5)
        self.assertAlmostEqual(report['errorShare'], 1.0)
        self.assertAlmostEqual(report['errorToFrameShareRatio'], 2.0)

    def test_error_share_rejects_empty_selection_and_reports_undefined(self):
        with self.assertRaises(ValueError):
            error_share(np.zeros((32, 8)), np.zeros((32, 8)), PHONES2, ())
        report = error_share(np.zeros((32, 8)), np.zeros((32, 8)), PHONES2, ('s',))
        self.assertIsNone(report['errorShare'])   # no error anywhere


if __name__ == '__main__':
    unittest.main()
