import unittest
import numpy as np

from tools.voice_model_training.phone_spectrum_separation import separation


def phones():
    return [dict(symbol='s', startFrame=0, endFrame=4096),
            dict(symbol='t', startFrame=4096, endFrame=8192),
            dict(symbol='k', startFrame=8192, endFrame=12288)]


class SeparationTests(unittest.TestCase):
    def test_distinct_reference_collapsed_prediction_gives_ratio_below_one(self):
        reference = np.zeros((48, 8))
        reference[0:16] = 0.; reference[16:32] = 5.; reference[32:48] = -5.
        candidate = np.zeros((48, 8))     # all phones identical -> zero distance
        report = separation(reference, candidate, phones(), ('s', 't', 'k'))
        self.assertGreater(report['referenceMeanDistance'], 1.)
        self.assertEqual(report['candidateMeanDistance'], 0.)
        self.assertEqual(report['separationRatio'], 0.)
        self.assertEqual(report['comparedPairs'], 3)

    def test_preserved_separation_is_about_one(self):
        reference = np.zeros((48, 8))
        reference[0:16, :2] = 3.; reference[32:48, :2] = -3.
        report = separation(reference, reference.copy(), phones(), ('s', 't', 'k'))
        self.assertAlmostEqual(report['separationRatio'], 1.0)

    def test_requires_two_distinct_symbols_and_contiguous_phones(self):
        frames = np.zeros((48, 8))
        with self.assertRaises(ValueError):
            separation(frames, frames, phones(), ('s',))
        with self.assertRaises(ValueError):
            separation(frames, frames, [dict(symbol='s', startFrame=1, endFrame=4096)], ('s', 't'))


if __name__ == '__main__':
    unittest.main()
