import unittest
from tools.voice_model_training.articulation_diagnostic import diagnose


class ArticulationTests(unittest.TestCase):
    def test_regions_and_independent_coverage(self):
        notes = [dict(startFrame=0, endFrame=4096, midi=69), dict(startFrame=4096, endFrame=8192, midi=None)]
        phones = [dict(startFrame=0, endFrame=1024, symbol='s'),
                  dict(startFrame=1024, endFrame=4096, symbol='a'),
                  dict(startFrame=4096, endFrame=8192, symbol='pau')]
        left = [dict(sourceFrame=i, f0Hz=440, voiced=True, confidence=1.) for i in (0, 1024, 2048, 4096, 7936)]
        right = [dict(row) for row in left]
        right[1].update(voiced=False, f0Hz=0.)
        right[2].update(f0Hz=220.)
        result = diagnose(notes, phones, left, right, frame_count=8192, window=2048)
        self.assertEqual(sum(g['analysisFrames'] for g in result['groups']), 5)
        vowel = next(g for g in result['groups'] if g['region']=='vowelInterior')
        self.assertEqual(vowel['reference']['measurable'], 2)
        self.assertEqual(vowel['candidate']['unvoiced'], 1)
        self.assertEqual(vowel['candidate']['medianAbsoluteCents'], 1200.)
        self.assertEqual(vowel['candidate']['within50'], 0)
        self.assertEqual({g['region'] for g in result['groups']}, {'boundary','vowelInterior','rest','padded'})

    def test_interval_gaps_are_not_silently_ignored(self):
        with self.assertRaises(ValueError):
            diagnose([dict(startFrame=1,endFrame=4096,midi=69)],
                     [dict(startFrame=0,endFrame=4096,symbol='a')], [], [], frame_count=4096,window=2048)


if __name__ == '__main__':
    unittest.main()
