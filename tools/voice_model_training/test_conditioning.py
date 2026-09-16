import copy
import unittest

from tools.voice_model_training.conditioning import build_conditioning


class ConditioningTests(unittest.TestCase):
    def test_boundaries_rests_slurs_and_partial_tail(self):
        label = dict(sourceId="phrase", frameCount=9, hopSize=2,
                     phonemes=[dict(symbol="sil", startFrame=0, endFrame=2, confidence=1),
                               dict(symbol="a", startFrame=2, endFrame=9, confidence=1)],
                     f0Hz=[0, 220, 225, 230, 240], voiced=[False, True, True, True, True], reviewRevision=None)
        score = dict(language="en", silencePhones=[0], syllables=[dict(lyric="ah", phoneStart=1, phoneEnd=2)],
                     notes=[dict(startFrame=0, endFrame=2, midi=None, syllable=None, slur=False),
                            dict(startFrame=2, endFrame=6, midi=0, syllable=0, slur=False),
                            dict(startFrame=6, endFrame=9, midi=60, syllable=0, slur=True)])
        original = copy.deepcopy((label, score))
        def build(**kw):
            return build_conditioning(label, score, vocabulary=["sil", "a"], minimum_confidence=.8, **kw)
        result = build()
        self.assertEqual(result["schemaVersion"], 2)
        self.assertEqual(result["conditioningRevision"], 2)
        rows = result["frames"]
        self.assertEqual([r["phoneId"] for r in rows], [1, 2, 2, 2, 2])
        self.assertEqual([r["noteIndex"] for r in rows], [0, 1, 1, 2, 2])
        self.assertEqual([r["rest"] for r in rows], [True, False, False, False, False])
        self.assertEqual([r["slur"] for r in rows], [False, False, False, True, True])
        self.assertEqual([r["validSamples"] for r in rows], [2, 2, 2, 2, 1])
        self.assertEqual([r["f0Hz"] for r in rows], label["f0Hz"])
        self.assertEqual((label, score), original)
        self.assertFalse(result["trainingAdmitted"])
        with self.assertRaises(ValueError): build(maximum_frames=4)
        label["phonemes"][1]["confidence"] = .1
        with self.assertRaises(ValueError): build()

    def test_conditioning_breathiness(self):
        label = dict(sourceId="phrase", frameCount=9, hopSize=2,
                     phonemes=[dict(symbol="sil", startFrame=0, endFrame=2, confidence=1),
                               dict(symbol="a", startFrame=2, endFrame=9, confidence=1)],
                     f0Hz=[0, 220, 225, 230, 240], voiced=[False, True, True, True, True], reviewRevision=None)
        score = dict(language="en", silencePhones=[0], syllables=[dict(lyric="ah", phoneStart=1, phoneEnd=2)],
                     notes=[dict(startFrame=0, endFrame=2, midi=None, syllable=None, slur=False),
                            dict(startFrame=2, endFrame=6, midi=0, syllable=0, slur=False),
                            dict(startFrame=6, endFrame=9, midi=60, syllable=0, slur=True)])
        def build(**kw):
            return build_conditioning(label, score, vocabulary=["sil", "a"], minimum_confidence=.8, **kw)
        # Default breathiness is 0.0 and hasBreathiness is False
        res_default = build()
        self.assertFalse(res_default["hasBreathiness"])
        self.assertEqual([r["breathiness"] for r in res_default["frames"]], [0.0, 0.0, 0.0, 0.0, 0.0])
        # Valid breathiness
        res_breath = build(breathiness=[0.0, 0.5, 0.2, 0.8, 1.0])
        self.assertTrue(res_breath["hasBreathiness"])
        self.assertEqual([r["breathiness"] for r in res_breath["frames"]], [0.0, 0.5, 0.2, 0.8, 1.0])
        # Length mismatch raises ValueError
        with self.assertRaises(ValueError):
            build(breathiness=[0.0, 0.5])
        # Out of bounds raises ValueError
        with self.assertRaises(ValueError):
            build(breathiness=[0.0, 1.5, 0.0, 0.0, 0.0])
        with self.assertRaises(ValueError):
            build(breathiness=[0.0, -0.1, 0.0, 0.0, 0.0])


if __name__ == "__main__":
    unittest.main()
