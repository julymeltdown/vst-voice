import copy
import unittest
from tools.voice_model_training.features import apply_pitch_features, pitch_corrections, rest_pitch_diagnostics


class FeatureTests(unittest.TestCase):
    def test_rest_voicing_distinguishes_interior_from_forward_window_overlap(self):
        score = dict(notes=[dict(startFrame=256, endFrame=1024, midi=None),
                            dict(startFrame=1024, endFrame=2048, midi=60)])
        features = dict(windowFrames=512, pitchFrames=[
            dict(sourceFrame=i * 256, voiced=i != 2) for i in range(6)])
        before = copy.deepcopy(features)
        result = rest_pitch_diagnostics(score, features)
        self.assertEqual(result['restAnalysisFrames'], 3)
        self.assertEqual(result['voicedInteriorWindows'], 1)
        self.assertEqual(result['voicedBoundaryWindows'], 1)
        self.assertEqual([r['sourceFrame'] for r in result['correctionQueue']], [256, 768])
        self.assertFalse(result['trainingAdmitted'])
        self.assertEqual(features, before)
        features['pitchFrames'][2]['voiced'] = True
        # A window ending exactly at the rest boundary is still wholly interior.
        self.assertEqual(rest_pitch_diagnostics(score, features)['voicedInteriorWindows'], 2)
        self.assertEqual(rest_pitch_diagnostics(dict(notes=[]), features)['restAnalysisFrames'], 0)

    def test_grid_settings_and_frame_values_reject(self):
        label = dict(sourceId="source", frameCount=257, hopSize=256,
            phonemes=[dict(symbol="a", startFrame=0, endFrame=257, confidence=1)],
            f0Hz=[0, 0], voiced=[False, False], reviewRevision="old")
        features = dict(formatId="com.project-seam.training-pitch-features", schemaVersion=1,
            sourceSha256="a" * 64, sampleRate=48000, frameCount=257, windowFrames=2048, hopSize=256,
            minimumHz=60, maximumHz=1200, voicingThreshold=0.32, algorithm="fft-autocorrelation-v1",
            coverage="full-hop-zero-padded", trainingAdmitted=False, releaseEligible=False,
            pitchFrames=[dict(sourceFrame=i * 256, f0Hz=220, voiced=True, confidence=0.9) for i in range(2)])
        def apply(value):
            return apply_pitch_features(label, value, source_sha256="a" * 64, sample_rate=48000,
                                        vocabulary={"a"}, minimum_confidence=0.8)
        self.assertEqual(apply(features)["f0Hz"], [220, 220])
        quality = copy.deepcopy(features)
        quality["pitchFrames"][0]["confidence"] = 0.5
        queue = pitch_corrections(label, quality, source_sha256="a" * 64, sample_rate=48000,
                                 vocabulary={"a"}, minimum_confidence=0.8)
        self.assertEqual([r["code"] for r in queue["correctionQueue"]],
                         ["low-pitch-confidence", "zero-padded-pitch-window", "zero-padded-pitch-window"])
        self.assertTrue(queue["reviewRequired"])
        for key, value in (("hopSize", 128), ("windowFrames", 1024), ("sampleRate", 44100),
                           ("schemaVersion", True), ("voicingThreshold", 0.5), ("coverage", "complete")):
            with self.assertRaises(ValueError): apply(dict(features, **{key: value}))
        for key, value in (("sourceFrame", 1), ("f0Hz", float("nan")), ("voiced", False),
                           ("confidence", float("inf")), ("confidence", -1)):
            broken = copy.deepcopy(features)
            broken["pitchFrames"][0][key] = value
            with self.assertRaises(ValueError): apply(broken)
        self.assertEqual(label["reviewRevision"], "old")


if __name__ == "__main__":
    unittest.main()
