import unittest
import numpy as np

from tools.voice_model_training.acoustic_frame_error import frame_error


PHONES = [dict(symbol='s', startFrame=0, endFrame=8192)]


class FrameErrorTests(unittest.TestCase):
    def test_perfect_prediction_scores_zero(self):
        generator = np.random.default_rng(3)
        target = generator.normal(0, 1, (32, 8))
        rows, uncovered = frame_error(target, target, PHONES)
        self.assertEqual(rows[0]['modelMeanAbsoluteError'], 0.0)
        self.assertEqual(rows[0]['modelRatioToConstant'], 0.0)
        self.assertEqual(uncovered, 0)

    def test_constant_prediction_has_ratio_one(self):
        generator = np.random.default_rng(5)
        target = generator.normal(0, 2, (32, 8))
        constant = np.repeat(target.mean(axis=0, keepdims=True), 32, axis=0)
        rows, _ = frame_error(target, constant, PHONES)
        self.assertAlmostEqual(rows[0]['modelRatioToConstant'], 1.0)
        self.assertGreater(rows[0]['referenceTemporalSpread'], 0)

    def test_model_beating_the_mean_is_below_one(self):
        target = np.linspace(0, 3, 32)[:, None] * np.ones((1, 8))
        rows, _ = frame_error(target, target + 1e-6, PHONES)
        self.assertLess(rows[0]['modelRatioToConstant'], 1.0)

    def test_short_interval_and_geometry_rejected(self):
        rows, _ = frame_error(np.ones((8, 8)), np.ones((8, 8)),
                              [dict(symbol='s', startFrame=0, endFrame=256)])
        self.assertEqual(rows[0]['measurements'], 'TOO_FEW_FRAMES')
        self.assertIsNone(rows[0]['modelRatioToConstant'])
        with self.assertRaises(ValueError):
            frame_error(np.ones((8, 8)), np.ones((8, 9)), PHONES)
        with self.assertRaises(ValueError):
            frame_error(np.ones((8, 8)), np.ones((8, 8)),
                        [dict(symbol='s', startFrame=1, endFrame=8192)])


if __name__ == '__main__':
    unittest.main()
