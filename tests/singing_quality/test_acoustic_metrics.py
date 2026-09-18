"""V06 acoustic measurements over retained packets; no musical acceptance claim."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools.singing_quality.acoustic_metrics import (
    ANALYZER_CEILING_HZ,
    DISPLACEMENT_LIMIT_SAMPLES,
    MEDIAN_CENTS_LIMIT,
    MINIMUM_CONFIDENCE,
    WITHIN_50_PERCENT_LIMIT,
    AcousticMetricError,
    PitchErrorReport,
    measure_pitch_error,
    load_phrase_measurements,
    summarise_case,
    timing_displacement_within_limit,
)
from tools.singing_quality.contract_types import CorpusError


def midi_hz(midi: int) -> float:
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def write_case(directory: Path, target_midi: int, vowel_onset: int, end: int,
               frames: list[dict], sample_rate: int = 48000) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    diagnostics = {
        "schema_version": 1,
        "sample_rate": sample_rate,
        "phrases": [{
            "phrase_id": "phrase-test",
            "target_timing": [{
                "unit_id": "unit-1",
                "target_midi": target_midi,
                "note_on_frame": vowel_onset,
                "desired_vowel_onset_frame": vowel_onset,
                "destination_start_frame": vowel_onset,
                "destination_end_frame": end,
            }],
            "rendered_placements": [{
                "unit_id": "unit-1",
                "aligned_start_frame": vowel_onset,
                "vowel_onset_frame": vowel_onset,
                "frame_count": end - vowel_onset,
                "requested_renderer": "classic-psola",
                "actual_renderer": "classic-psola",
                "used_fallback": False,
            }],
        }],
    }
    (directory / "diagnostics.json").write_text(json.dumps(diagnostics), encoding="utf-8")
    analysis = directory / "analysis"
    analysis.mkdir(parents=True, exist_ok=True)
    (analysis / "analysis.json").write_text(
        json.dumps({"pitchFrames": frames}), encoding="utf-8")


def voiced(index: int, hz: float, confidence: float = 0.95) -> dict:
    return {"sourceFrame": index, "voiced": True, "f0Hz": hz, "confidence": confidence}


def unvoiced(index: int) -> dict:
    return {"sourceFrame": index, "voiced": False, "f0Hz": 0.0, "confidence": 0.0}


class PitchThresholdTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_accurate_steady_frames_pass_frozen_limits(self) -> None:
        # Given: voiced frames within 10 cents of the target note.
        target = midi_hz(67)
        frames = [voiced(48000 + 256 * i, target * (2.0 ** (10.0 / 1200.0)))
                  for i in range(20)]
        case = self.root / "accurate"
        write_case(case, 67, 48000, 60000, frames)
        # When: the case is summarised.
        summary = summarise_case(case, "accurate")
        # Then: it passes the frozen numeric thresholds without qualification.
        self.assertTrue(summary.pitch.within_limits())
        self.assertLessEqual(summary.pitch.median_absolute_cents, MEDIAN_CENTS_LIMIT)
        self.assertGreaterEqual(summary.pitch.within_50_percent, WITHIN_50_PERCENT_LIMIT)
        self.assertEqual(0, summary.pitch.octave_errors)

    def test_inaccurate_steady_frames_fail_frozen_limits(self) -> None:
        # Given: voiced frames 120 cents off the target note.
        target = midi_hz(67)
        frames = [voiced(48000 + 256 * i, target * (2.0 ** (120.0 / 1200.0)))
                  for i in range(20)]
        case = self.root / "off-pitch"
        write_case(case, 67, 48000, 60000, frames)
        summary = summarise_case(case, "off-pitch")
        # Then: the measurement is retained but does not pass.
        self.assertFalse(summary.pitch.within_limits())
        self.assertGreater(summary.pitch.median_absolute_cents, MEDIAN_CENTS_LIMIT)

    def test_unvoiced_frames_never_count_as_accurate_pitch(self) -> None:
        # Given: a steady span that is entirely unvoiced.
        frames = [unvoiced(48000 + 256 * i) for i in range(10)]
        case = self.root / "unvoiced"
        write_case(case, 67, 48000, 60000, frames)
        summary = summarise_case(case, "unvoiced")
        # Then: nothing is scored and the case cannot pass.
        self.assertEqual(0, summary.pitch.scored_frames)
        self.assertEqual(10, summary.pitch.unvoiced_frames)
        self.assertFalse(summary.pitch.within_limits())

    def test_low_confidence_frames_are_reported_not_scored(self) -> None:
        # Given: voiced frames below the confidence floor.
        target = midi_hz(67)
        frames = [voiced(48000 + 256 * i, target, confidence=MINIMUM_CONFIDENCE - 0.1)
                  for i in range(10)]
        case = self.root / "low-confidence"
        write_case(case, 67, 48000, 60000, frames)
        summary = summarise_case(case, "low-confidence")
        self.assertEqual(0, summary.pitch.scored_frames)
        self.assertEqual(10, summary.pitch.low_confidence_frames)
        self.assertFalse(summary.pitch.within_limits())

    def test_analyzer_saturation_frames_are_excluded_from_scoring(self) -> None:
        # Given: frames at the analyzer F0 ceiling plus accurate frames.
        target = midi_hz(67)
        frames = ([voiced(48000 + 256 * i, ANALYZER_CEILING_HZ) for i in range(10)]
                  + [voiced(48000 + 256 * (10 + i), target) for i in range(10)])
        case = self.root / "saturated"
        write_case(case, 67, 48000, 60000, frames)
        summary = summarise_case(case, "saturated")
        # Then: ceiling frames are named as saturation, not octave errors, and
        # do not corrupt the measured error of the credible frames.
        self.assertEqual(10, summary.pitch.saturated_frames)
        self.assertEqual(0, summary.pitch.octave_errors)
        self.assertEqual(10, summary.pitch.scored_frames)
        self.assertTrue(summary.pitch.within_limits())
        payload = json.loads(summary.to_json())
        self.assertEqual(ANALYZER_CEILING_HZ, payload["pitch"]["analyzer_ceiling_hz"])
        self.assertEqual("none", payload["qualification"])

    def test_octave_lock_is_reported_separately_from_median(self) -> None:
        # Given: a voiced frame one octave above the target.
        target = midi_hz(67)
        frames = [voiced(48000, target * 2.0)]
        case = self.root / "octave"
        write_case(case, 67, 48000, 48300, frames)
        summary = summarise_case(case, "octave")
        self.assertEqual(1, summary.pitch.octave_errors)
        self.assertFalse(summary.pitch.within_limits())


class TimingPlacementTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_vowel_onset_must_land_on_the_requested_anchor(self) -> None:
        frames = [voiced(48000 + 256 * i, midi_hz(67)) for i in range(10)]
        case = self.root / "aligned"
        write_case(case, 67, 48000, 60000, frames)
        summary = summarise_case(case, "aligned")
        self.assertTrue(summary.timing_displacement_ok)
        self.assertTrue(all(placement.vowel_displacement == 0
                            for placement in summary.placements
                            if placement.vowel_displacement is not None))

    def test_displacement_beyond_one_sample_is_rejected(self) -> None:
        frames = [voiced(48000 + 256 * i, midi_hz(67)) for i in range(10)]
        case = self.root / "shifted"
        write_case(case, 67, 48000, 60000, frames)
        diagnostics = json.loads((case / "diagnostics.json").read_text())
        diagnostics["phrases"][0]["rendered_placements"][0]["vowel_onset_frame"] = (
            48000 + DISPLACEMENT_LIMIT_SAMPLES + 1)
        (case / "diagnostics.json").write_text(json.dumps(diagnostics))
        summary = summarise_case(case, "shifted")
        self.assertFalse(summary.timing_displacement_ok)


class PacketRejectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_missing_diagnostics_is_rejected(self) -> None:
        case = self.root / "missing"
        case.mkdir()
        # A missing retained artifact is a corpus rejection, not a measurement.
        with self.assertRaises(CorpusError):
            summarise_case(case, "missing")

    def test_target_and_placement_count_mismatch_is_rejected(self) -> None:
        frames = [voiced(48000, midi_hz(67))]
        case = self.root / "mismatch"
        write_case(case, 67, 48000, 48300, frames)
        diagnostics = json.loads((case / "diagnostics.json").read_text())
        diagnostics["phrases"][0]["target_timing"].append(
            diagnostics["phrases"][0]["target_timing"][0])
        (case / "diagnostics.json").write_text(json.dumps(diagnostics))
        with self.assertRaises(AcousticMetricError):
            summarise_case(case, "mismatch")

    def test_sample_rate_is_read_from_the_packet(self) -> None:
        frames = [voiced(48000, midi_hz(67))]
        case = self.root / "rate"
        write_case(case, 67, 48000, 48300, frames, sample_rate=44100)
        phrases = load_phrase_measurements(case)
        self.assertEqual(44100, phrases[0].sample_rate)


if __name__ == "__main__":
    unittest.main()
