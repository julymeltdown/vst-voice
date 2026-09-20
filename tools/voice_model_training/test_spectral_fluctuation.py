import unittest
import numpy as np

from tools.voice_model_training.spectral_fluctuation import fluctuation, per_phone


class FluctuationTests(unittest.TestCase):
    def test_noise_fluctuates_more_than_a_smooth_ramp(self):
        generator = np.random.default_rng(71)
        noisy = generator.normal(0, 1, (32, 8))
        smooth = np.linspace(0, 1, 32)[:, None] * np.ones((1, 8))
        self.assertGreater(fluctuation(noisy).mean(), fluctuation(smooth).mean() + .1)

    def test_normalization_removes_overall_gain(self):
        generator = np.random.default_rng(7)
        frames = generator.normal(0, 1, (32, 8))
        self.assertTrue(np.allclose(fluctuation(frames), fluctuation(frames * 50)))
        self.assertFalse(np.allclose(fluctuation(frames, normalize=False),
                                     fluctuation(frames * 50, normalize=False)))

    def test_pairing_and_retained_fraction(self):
        # 30 fluctuation rows correspond to 31 analysis frames = 7936 samples.
        phones = [dict(symbol='s', startFrame=0, endFrame=7936)]
        # Inputs are already per-frame fluctuation values, length T-1 for T frames.
        reference, candidate = np.full(30, 1.0), np.full(30, .25)
        rows, uncovered = per_phone(reference, candidate, phones)
        self.assertEqual(uncovered, 0)
        self.assertAlmostEqual(rows[0]['retainedFraction'], .25)
        self.assertAlmostEqual(rows[0]['candidateMinusReference'], -.75)

    def test_short_interval_and_mismatched_geometry_are_explicit(self):
        rows, _ = per_phone(np.full(8, 1.), np.full(8, 1.),
                            [dict(symbol='s', startFrame=0, endFrame=256)])
        self.assertEqual(rows[0]['measurements'], 'TOO_FEW_FRAMES')
        self.assertIsNone(rows[0]['retainedFraction'])
        with self.assertRaises(ValueError):
            per_phone(np.full(8, 1.), np.full(9, 1.), [dict(symbol='s', startFrame=0, endFrame=2048)])

    def test_zero_reference_does_not_divide_by_zero(self):
        # A zero reference fluctuation means the ratio is undefined, not infinite.
        rows, _ = per_phone(np.zeros(30), np.ones(30) * 2, [dict(symbol='s', startFrame=0, endFrame=7936)])
        self.assertIsNone(rows[0]['retainedFraction'])
        self.assertAlmostEqual(rows[0]['candidateMinusReference'], 2.0)


if __name__ == '__main__':
    unittest.main()
