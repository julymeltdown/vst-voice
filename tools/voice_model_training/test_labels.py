import copy
import unittest
from tools.voice_model_training.labels import label_report, score_report


class LabelTests(unittest.TestCase):
    def test_explicit_silence_ownership(self):
        score = dict(language="ko", silencePhones=[0, 2, 4], syllables=[
            dict(lyric="아", phoneStart=1, phoneEnd=2), dict(lyric="오", phoneStart=3, phoneEnd=4)],
            notes=[dict(startFrame=0, endFrame=250, midi=60, syllable=0, slur=False),
                   dict(startFrame=250, endFrame=500, midi=62, syllable=1, slur=False)])
        self.assertEqual(score_report(score, frame_count=500, phoneme_count=5,
                                      explicit_silence=True)["silencePhoneCount"], 3)
        for indices in ([0, 2], [0, 1, 2, 4], [0, 2, 2, 4], [4, 2, 0], [False, 2, 4], [0, 2, 5]):
            broken = dict(score, silencePhones=indices)
            with self.assertRaises(ValueError):
                score_report(broken, frame_count=500, phoneme_count=5, explicit_silence=True)
        with self.assertRaises(ValueError):
            score_report(score, frame_count=500, phoneme_count=5)

    def test_score_melisma_and_rest(self):
        score = dict(language="ja", syllables=[dict(lyric="あ", phoneStart=0, phoneEnd=1)], notes=[
            dict(startFrame=0, endFrame=100, midi=None, syllable=None, slur=False),
            dict(startFrame=100, endFrame=300, midi=60, syllable=0, slur=False),
            dict(startFrame=300, endFrame=500, midi=62, syllable=0, slur=True)])
        before = copy.deepcopy(score)
        self.assertEqual(score_report(score, frame_count=500, phoneme_count=1)["slurCount"], 1)
        self.assertEqual(score, before)
        for key, value in (("slur", False), ("syllable", 1), ("midi", True), ("startFrame", 301)):
            broken = copy.deepcopy(score)
            broken["notes"][2][key] = value
            with self.assertRaises(ValueError):
                score_report(broken, frame_count=500, phoneme_count=1)
        score["notes"][1]["slur"] = True
        with self.assertRaises(ValueError):
            score_report(score, frame_count=500, phoneme_count=1)

    def setUp(self):
        self.label = dict(sourceId="source", frameCount=500, hopSize=256,
                          phonemes=[dict(symbol="a", startFrame=0, endFrame=500, confidence=0.9)],
                          f0Hz=[220.0, 221.0], voiced=[True, True], reviewRevision="supplied-revision")

    def report(self):
        return label_report(self.label, vocabulary={"a", "SP"}, minimum_confidence=0.8)

    def test_consistency_is_not_training_approval(self):
        before = copy.deepcopy(self.label)
        result = self.report()
        self.assertTrue(result["consistencyPassed"])
        self.assertFalse(result["trainingAdmitted"])
        self.assertFalse(result["reviewAuthenticated"])
        self.assertEqual(self.label, before)

    def test_uncertain_unknown_and_unreviewed_labels_queue(self):
        self.label["phonemes"][0].update(symbol="unknown", confidence=0.2)
        self.label["reviewRevision"] = None
        self.label["voiced"][1] = False
        self.assertEqual({item["code"] for item in self.report()["correctionQueue"]},
                         {"unknown-phone", "low-alignment-confidence", "review-revision-missing", "voicing-f0-mismatch"})

    def test_outside_phrase_and_bad_feature_geometry_reject(self):
        self.label["phonemes"][0]["endFrame"] = 501
        with self.assertRaises(ValueError): self.report()
        self.label["phonemes"][0]["endFrame"] = 500
        self.label["f0Hz"] = [220]
        with self.assertRaises(ValueError): self.report()


if __name__ == "__main__":
    unittest.main()
