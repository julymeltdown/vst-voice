import importlib.util
import unittest

from tools.voice_model_training.acoustic_reconstruction import mel_metrics, phrase_durations, require_validation_sources


class ReconstructionTest(unittest.TestCase):
    def test_probe_never_uses_training_or_final_test_set(self):
        snapshot = dict(bindings=dict(split=dict(groups=[dict(partition=kind, sourceIds=[kind])
                                                        for kind in ("train", "validation", "test")])))
        require_validation_sources(snapshot, ["validation"])
        for ids in ([], ["train"], ["test"], ["missing"], ["validation", "validation"]):
            with self.assertRaises(ValueError):
                require_validation_sources(snapshot, ids)

    def test_durations_preserve_zero_length_and_repeated_phones(self):
        batch = dict(tokens=[1, 1, 2], mel2ph=[1, 1, 3], frameOffset=0, phraseAnalysisFrames=3)
        self.assertEqual(phrase_durations(batch), [2, 0, 1])
        for updates in (dict(mel2ph=[1, 3, 1]), dict(frameOffset=1), dict(mel2ph=[0, 1, 2]),
                        dict(phraseAnalysisFrames=4)):
            with self.assertRaises(ValueError):
                phrase_durations(batch | updates)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "NumPy required")
    def test_metrics_separate_pauses_and_reject_invalid_predictions(self):
        import numpy as np
        target = np.array([[0., 0.], [1., 1.]])
        predicted = np.array([[2., 2.], [2., 2.]])
        result = mel_metrics(predicted, target, [True, False])
        self.assertEqual(result["meanAbsoluteError"], 1.5)
        self.assertEqual(result["pauseMeanAbsoluteError"], 2.)
        self.assertEqual(result["nonPauseMeanAbsoluteError"], 1.)
        self.assertAlmostEqual(result["rootMeanSquareError"], 2.5**.5)
        self.assertIsNone(mel_metrics(target, target, [False, False])["pauseMeanAbsoluteError"])
        for value in (np.full_like(target, np.nan), np.full_like(target, np.inf), target[:1]):
            with self.assertRaises(ValueError):
                mel_metrics(value, target, [True, False])
