import unittest
import hashlib
import json
from pathlib import Path
import tempfile
import numpy as np

from tools.voice_model_training.score_application_export import inspect, locate_errors, measure_rests
from tools.voice_model_training.test_compare_application_export import pcm_wav
from tools.voice_model_training.test_pitch_comparison import track


class ScoreExportTests(unittest.TestCase):
    def test_bound_receipts_master_and_score_clock(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            master, labels, comparison = [root / name for name in ('master.wav', 'labels.json', 'comparison.json')]
            payload = pcm_wav(np.zeros((4096, 2)))
            master.write_bytes(payload)
            config = dict(formatId='com.project-seam.voice-training-label-config', schemaVersion=3,
                sampleRate=48000, labels=[dict(sourceSha256='a' * 64,
                    label=dict(frameCount=4096, phonemes=[{}]),
                    score=dict(language='en', silencePhones=[],
                        syllables=[dict(lyric='a', phoneStart=0, phoneEnd=1)],
                        notes=[dict(startFrame=0, endFrame=4096, midi=69, syllable=0, slur=False)]))])
            captured = dict(formatId='com.project-seam.application-audio-comparison', schemaVersion=1,
                referenceSha256='a' * 64, candidateSha256=hashlib.sha256(payload).hexdigest(),
                candidate=dict(sampleRate=48000, frameCount=4096, channels=2, sampleWidthBytes=2),
                pitch=dict(reference=dict(sourceSha256='a' * 64), candidate=dict(sourceSha256='b' * 64),
                    referenceTrack=track([220.] * 16), candidateTrack=track([440.] * 16, digest='b' * 64)))
            labels.write_text(json.dumps(config))
            comparison.write_text(json.dumps(captured))
            def run():
                return inspect(comparison, hashlib.sha256(comparison.read_bytes()).hexdigest(),
                    labels, hashlib.sha256(labels.read_bytes()).hexdigest(), master)
            result = run()
            self.assertEqual(result['strictPitchStatus'], 'MISMATCH')
            self.assertEqual(result['errorLocations']['counts']['candidateWithin50'], 9)
            self.assertEqual(result['strictPitchSummary']['unmeasurableFrames'], 7)
            self.assertIsNone(result['silence']['allRestsExactlyZero'])
            config['sampleRate'] = 44100
            labels.write_text(json.dumps(config))
            with self.assertRaisesRegex(ValueError, 'clocks differ'):
                run()
            master.write_bytes(pcm_wav(np.ones((4096, 2)) * .25))
            with self.assertRaisesRegex(ValueError, 'Master differs'):
                run()

    def test_reference_error_is_not_automatically_candidate_error(self):
        notes = [dict(startFrame=0, endFrame=4096, midi=69)]
        report = locate_errors(notes,
            [dict(sourceFrame=0, f0Hz=220), dict(sourceFrame=256, f0Hz=440)],
            [dict(sourceFrame=0, f0Hz=440), dict(sourceFrame=256, f0Hz=220)],
            [1200, -1200], frame_count=4096, window=2048)
        self.assertEqual(report['counts']['mismatches'], 2)
        self.assertEqual(report['counts']['referenceWithin50'], 1)
        self.assertEqual(report['counts']['candidateWithin50'], 1)
        self.assertEqual(report['mismatchingFrames'][0]['referenceScoreCents'], -1200)
        self.assertFalse(report['scoreIsExpressivePitchGroundTruth'])

    def test_boundary_equality_transition_rest_and_padding(self):
        notes = [dict(startFrame=0, endFrame=2048, midi=69),
                 dict(startFrame=2048, endFrame=8192, midi=None)]
        rows = [dict(sourceFrame=i, f0Hz=440) for i in (0, 256, 2048, 7936)]
        report = locate_errors(notes, rows, rows, [60] * 4, frame_count=8192, window=2048)
        self.assertEqual([r['location'] for r in report['mismatchingFrames']],
                         ['noteInterior', 'transition', 'rest', 'padded'])
        self.assertEqual(report['counts']['mismatches'], 4)

    def test_no_rounding_or_octave_exclusion(self):
        rows = [dict(sourceFrame=i * 256, f0Hz=440) for i in range(4)]
        report = locate_errors([dict(startFrame=0, endFrame=4096, midi=69)],
            rows, rows, [None, 50, 50.000001, -1200], frame_count=4096, window=2048)
        self.assertEqual(report['counts']['mismatches'], 2)
        with self.assertRaisesRegex(ValueError, 'Unequal'):
            locate_errors([], rows, [], [], frame_count=4096, window=2048)

    def test_rest_checks_each_channel_without_cancellation(self):
        notes = [dict(startFrame=0, endFrame=2, midi=None)]
        report = measure_rests(notes, np.asarray([[.1, -.1], [0., 0.]]))
        self.assertFalse(report['allRestsExactlyZero'])
        self.assertEqual(report['rests'][0]['channelNonzeroSamples'], [1, 1])
        self.assertTrue(measure_rests(notes, np.zeros((2, 2)))['allRestsExactlyZero'])
        self.assertIsNone(measure_rests([], np.zeros((2, 2)))['allRestsExactlyZero'])
        with self.assertRaisesRegex(ValueError, 'outside'):
            measure_rests(notes, np.zeros((1, 2)))


if __name__ == '__main__':
    unittest.main()
