"""Framewise diagnostics; synthetic controls never qualify a singer."""
from copy import deepcopy
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.pitch_comparison import compare_pitch_tracks, compare_wavs, main


def track(pitches, *, digest="a" * 64, confidence=.95):
    return dict(formatId="com.project-seam.training-pitch-features", schemaVersion=1,
        sourceSha256=digest, sampleRate=48000, frameCount=len(pitches) * 256,
        windowFrames=2048, hopSize=256, minimumHz=60, maximumHz=1200,
        voicingThreshold=.32, algorithm="fft-autocorrelation-v1", coverage="full-hop-zero-padded",
        trainingAdmitted=False, releaseEligible=False,
        pitchFrames=[dict(sourceFrame=i * 256, f0Hz=p, voiced=p > 0,
                          confidence=confidence if p > 0 else 0.) for i, p in enumerate(pitches)])


def compare(reference, candidate, **changes):
    return compare_pitch_tracks(reference, candidate, reference_sha256=reference["sourceSha256"],
        candidate_sha256=candidate["sourceSha256"], sample_rate=48000,
        frame_count=reference["frameCount"], **changes)


def wav(samples):
    pcm = struct.pack("<" + "f" * len(samples), *samples)
    return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(pcm), b"WAVE", b"fmt ",
                       16, 3, 1, 48000, 192000, 4, 32, b"data", len(pcm)) + pcm


class PitchComparisonTests(unittest.TestCase):
    def test_reversed_melody_same_median_octave_and_displacement_fail(self):
        pitches = [220.] * 24 + [330.] * 24 + [440.] * 24
        source = track(pitches)
        self.assertEqual(compare(source, source)["status"], "MATCH_ON_MEASURABLE_FRAMES")
        for name, values in (("reversed", list(reversed(pitches))),
                             ("octave", [2 * p for p in pitches]),
                             ("displaced", [0.] * 12 + pitches[:-12])):
            with self.subTest(name=name):
                result = compare(source, track(values))
                self.assertEqual(result["status"], "MISMATCH")
                self.assertFalse(result["comparisonSatisfied"])
                self.assertGreater(result["outsideToleranceFrames"] + result["voicingMismatchFrames"], 0)
                self.assertFalse(result["releaseEligible"])
        octave = compare(source, track([2 * p for p in pitches]))
        self.assertAlmostEqual(octave["rootMeanSquareCents"], 1200.)
        self.assertEqual(octave["alignment"], "exact-source-frame-no-shift-no-warp")

    def test_signed_pitch_errors_cannot_cancel(self):
        source = track([220.] * 39)
        shifted = track([440.] * 16 + [110.] * 16 + [220.] * 7)
        result = compare(source, shifted)
        self.assertEqual(result["meanAbsoluteCents"], 1200.)
        self.assertEqual(result["outsideToleranceFrames"], 32)
        self.assertEqual(result["status"], "MISMATCH")

    def test_voicing_and_unmeasurable_spans_are_separate(self):
        source = track([220.] * 32)
        unvoiced = track([0.] * 32)
        mismatch = compare(source, unvoiced)
        self.assertEqual(mismatch["status"], "MISMATCH")
        self.assertEqual(mismatch["voicingMismatchFrames"], 25)
        self.assertEqual(mismatch["edgeWindowFrames"], 7)
        self.assertIsNone(mismatch["rootMeanSquareCents"])
        silent = compare(unvoiced, unvoiced)
        self.assertEqual(silent["status"], "UNRESOLVED")
        self.assertEqual(silent["unvoicedPairs"], 25)
        self.assertEqual(silent["measurableVoicedPairs"], 0)
        uncertain = deepcopy(source)
        uncertain["pitchFrames"][3]["confidence"] = .4
        result = compare(source, uncertain)
        self.assertEqual(result["status"], "UNRESOLVED")
        self.assertEqual(result["lowConfidenceFrames"], 1)
        self.assertEqual(result["unmeasurableSpans"][0], dict(startFrame=768, endFrame=1024,
            analysisFrameCount=1, reasons=["candidate-low-confidence"]))
        self.assertIsNone(result["frameErrorsCents"][3])

    def test_unequal_grids_clock_source_binding_and_invalid_frames_rejected(self):
        source = track([220.] * 32)
        for changes in (dict(hopSize=128), dict(sampleRate=44100), dict(frameCount=8191),
                        dict(windowFrames=1024), dict(sourceSha256="b" * 64),
                        dict(coverage="complete-windows"), dict(schemaVersion=True),
                        dict(minimumHz=50), dict(trainingAdmitted=True)):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                compare_pitch_tracks(source, source | changes, reference_sha256="a" * 64,
                    candidate_sha256="a" * 64, sample_rate=48000, frame_count=8192)
        for changes in (dict(sourceFrame=1), dict(f0Hz=float("nan")), dict(voiced=False),
                        dict(confidence=-1), dict(confidence=2), dict(voiced=True, f0Hz=0)):
            malformed = deepcopy(source)
            malformed["pitchFrames"][0].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError): compare(source, malformed)
        with self.assertRaises(ValueError): compare(source, track([220.] * 31))
        for options in (dict(minimum_confidence=0), dict(minimum_confidence=float("nan")),
                        dict(maximum_error_cents=-1), dict(maximum_error_cents=True)):
            with self.assertRaises(ValueError): compare(source, source, **options)

    def test_wav_comparison_binds_extracted_tracks_and_retains_cli_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.wav"
            source.write_bytes(wav([0.] * 8192))
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            captured = track([220.] * 32, digest=digest)
            with patch("tools.voice_model_training.pitch_comparison.extract_pitch", return_value=captured):
                result = compare_wavs(source, source, executable=Path(__file__))
                self.assertEqual(result["reference"]["sourceSha256"], digest)
                self.assertEqual(result["comparison"]["status"], "MATCH_ON_MEASURABLE_FRAMES")
                output = root / "comparison.json"
                with patch("builtins.print"):
                    self.assertEqual(main(["--reference", str(source), "--candidate", str(source),
                        "--extractor", __file__, "--output", str(output)]), 0)
                self.assertEqual(json.loads(output.read_bytes())["reference"]["sourceBytes"], source.stat().st_size)
            with patch("tools.voice_model_training.pitch_comparison.extract_pitch", return_value=track([220.] * 32)):
                with self.assertRaisesRegex(ValueError, "binding"):
                    compare_wavs(source, source, executable=Path(__file__))
            other = root / "different.wav"
            other.write_bytes(wav([0.] * 8191))
            with patch("tools.voice_model_training.pitch_comparison.extract_pitch") as extract:
                with self.assertRaisesRegex(ValueError, "geometry"):
                    compare_wavs(source, other, executable=Path(__file__))
                extract.assert_not_called()

    @unittest.skipUnless(os.environ.get("SEAM_PITCH_COMPARISON_EXTRACTOR"), "Optional native extractor controls")
    def test_actual_native_estimator_positive_and_negative_waveform_controls(self):
        executable = Path(os.environ["SEAM_PITCH_COMPARISON_EXTRACTOR"])
        pitches = [220.] * 8192 + [330.] * 8192 + [440.] * 8192
        def signal(values):
            phase, result = 0., []
            for p in values:
                phase += 2 * math.pi * p / 48000
                result.append(.4 * math.sin(phase) if p else 0.)
            return result
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.wav"
            source.write_bytes(wav(signal(pitches)))
            cases = {"same": pitches, "reversed": list(reversed(pitches)),
                "octave": [2*p for p in pitches], "delayed": [0.] * 4096 + pitches[:-4096],
                "unvoiced": [0.] * len(pitches)}
            for name, values in cases.items():
                candidate = root / (name + ".wav")
                candidate.write_bytes(wav(signal(values)))
                report = compare_wavs(source, candidate, executable=executable)["comparison"]
                with self.subTest(name=name):
                    self.assertEqual(report["status"], "MATCH_ON_MEASURABLE_FRAMES" if name == "same" else "MISMATCH")


if __name__ == "__main__":
    unittest.main()
