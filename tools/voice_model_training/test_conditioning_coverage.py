import unittest

from tools.voice_model_training.conditioning_coverage import channel_coverage, summarize


PHONES = [dict(symbol='s'), dict(symbol='a')]
FRAMES = [dict(phoneIndex=0, breathiness=0.0, f0Hz=0.0),
          dict(phoneIndex=0, breathiness=0.4, f0Hz=0.0),
          dict(phoneIndex=1, breathiness=0.0, f0Hz=220.0)]


class CoverageTests(unittest.TestCase):
    def test_reports_populated_and_dead_channels(self):
        report = summarize(FRAMES, PHONES)
        self.assertEqual(report['channels']['breathiness']['nonzero'], 1)
        self.assertEqual(report['channels']['f0Hz']['nonzero'], 1)
        self.assertAlmostEqual(report['channels']['breathiness']['maximum'], 0.4)
        self.assertEqual(report['frameCount'], 3)

    def test_all_zero_channel_is_visible_not_hidden(self):
        frames = [dict(frame, breathiness=0.0) for frame in FRAMES]
        report = summarize(frames, PHONES)
        self.assertEqual(report['channels']['breathiness']['nonzero'], 0)
        self.assertEqual(report['channels']['breathiness']['maximum'], 0.0)

    def test_channel_coverage_is_per_symbol(self):
        coverage = channel_coverage(FRAMES, PHONES, 'breathiness')
        self.assertAlmostEqual(coverage['s']['coverage'], 0.5)
        self.assertEqual(coverage['a']['coverage'], 0.0)

    def test_invalid_inputs(self):
        with self.assertRaises(ValueError):
            summarize([], PHONES)
        with self.assertRaises(ValueError):
            summarize([dict(phoneIndex=5, breathiness=0.0)], PHONES)
        with self.assertRaises(ValueError):
            summarize([dict(phoneIndex=0)], PHONES)
        with self.assertRaises(ValueError):
            channel_coverage(FRAMES, PHONES, 'voicing')


if __name__ == '__main__':
    unittest.main()
