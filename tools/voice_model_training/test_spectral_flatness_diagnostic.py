import unittest
import numpy as np

from tools.voice_model_training.spectral_flatness_diagnostic import (
    compare, per_phone_flatness, spectral_flatness)


class FlatnessTests(unittest.TestCase):
    def test_tone_is_flatness_low_and_noise_is_high(self):
        tone = np.zeros((3, 16)); tone[:, 0] = 1.
        noise = np.ones((3, 16))
        self.assertLess(spectral_flatness(tone)[0], .05)
        self.assertAlmostEqual(spectral_flatness(noise)[0], 1.)

    def test_phone_mapping_and_short_interval_are_explicit(self):
        flatness = np.full(16, .5)
        rows, uncovered = per_phone_flatness(flatness, [dict(symbol='s', startFrame=0, endFrame=2560),
                                                        dict(symbol='s', startFrame=2560, endFrame=2816)])
        self.assertEqual(rows[0]['measurements'], 'MEASURED')
        self.assertEqual(rows[1]['measurements'], 'TOO_FEW_FRAMES')
        self.assertIsNone(rows[1]['meanFlatness'])
        # flatness spans 16 hops (4096 samples); the phones cover only 2816,
        # so the trailing 5 analysis frames are genuinely uncovered.
        self.assertEqual(uncovered, 5)

    def test_signed_difference_and_alignment(self):
        phones = [dict(symbol='s', startFrame=0, endFrame=5120)]
        report = compare(np.full(20, .9), np.full(20, .3), phones)
        row = report['rows'][0]
        self.assertAlmostEqual(row['candidateMinusReference'], -.6)
        self.assertEqual(row['phone'], 's')
        self.assertEqual(report['uncoveredAnalysisFrames'], 0)
        with self.assertRaises(ValueError):
            compare(np.full(20, .9), np.full(20, .3), [dict(symbol='s', startFrame=1, endFrame=5120)])

    def test_invalid_inputs(self):
        for bad in (np.full((2, 2), -1.), np.full((2, 2), np.nan)):
            with self.assertRaises(ValueError):
                spectral_flatness(bad)
        with self.assertRaises(ValueError):
            per_phone_flatness(np.full(4, 1.5), [dict(symbol='s', startFrame=0, endFrame=128)])


if __name__ == '__main__':
    unittest.main()
