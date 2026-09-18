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
    load_pilot_pitch,
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


class PilotPitchReportTests(unittest.TestCase):
    """The singer pilot records per-note pitch; this re-expresses it as a measurement."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_pilot(self, notes: list[dict], status: str = "DIAGNOSTIC_NOT_QUALIFICATION") -> Path:
        path = self.root / "baseline-pitch.json"
        path.write_text(json.dumps({
            "status": status,
            "audioSha256": "a" * 64,
            "windowPolicy": "central-half-full-analysis-windows-no-data-dependent-exclusions",
            "notes": notes,
        }), encoding="utf-8")
        return path

    @staticmethod
    def note(index: int, hz: float, median: float | None, voiced: int = 16,
             within: int | None = None, large: int = 0) -> dict:
        return {
            "noteIndex": index,
            "expectedHz": hz,
            "windowStartTick": index * 480,
            "windowEndTick": index * 480 + 240,
            "windowStartFrame": 3000,
            "windowEndFrame": 9000,
            "analysisFrames": voiced,
            "voicedFrames": voiced,
            "within50CentsFrames": voiced if within is None else within,
            "largePitchErrorFrames": large,
            "medianAbsoluteCents": median,
        }

    def test_accurate_range_reports_a_pass_against_frozen_limits(self) -> None:
        path = self.write_pilot([
            self.note(0, 261.6, 0.35), self.note(1, 329.6, 0.09),
            self.note(2, 523.3, 0.23),
        ])
        report = load_pilot_pitch(path, "baseline")
        self.assertEqual(3, len(report.notes))
        self.assertLess(report.median_absolute_cents(), MEDIAN_CENTS_LIMIT)
        self.assertEqual(100.0, report.within_50_percent())
        self.assertTrue(report.within_limits())
        payload = json.loads(report.to_json())
        self.assertEqual("none", payload["qualification"])
        self.assertEqual("singer-pilot", payload["source"])
        self.assertEqual(261.6, payload["lowest_target_hz"])
        self.assertEqual(523.3, payload["highest_target_hz"])

    def test_large_pitch_error_frames_prevent_a_pass(self) -> None:
        path = self.write_pilot([
            self.note(0, 261.6, 0.35),
            self.note(1, 329.6, 0.09, large=1, within=15),
        ])
        report = load_pilot_pitch(path, "baseline")
        self.assertEqual(1, report.large_pitch_error_frames())
        self.assertFalse(report.within_limits())

    def test_median_above_threshold_prevents_a_pass(self) -> None:
        path = self.write_pilot([self.note(0, 261.6, MEDIAN_CENTS_LIMIT + 5.0)])
        report = load_pilot_pitch(path, "baseline")
        self.assertFalse(report.within_limits())

    def test_missing_expected_frequency_is_rejected(self) -> None:
        path = self.write_pilot([{"noteIndex": 0, "medianAbsoluteCents": 1.0}])
        with self.assertRaises(AcousticMetricError):
            load_pilot_pitch(path, "baseline")

    def test_empty_notes_are_rejected(self) -> None:
        path = self.write_pilot([])
        with self.assertRaises(AcousticMetricError):
            load_pilot_pitch(path, "baseline")

    def test_null_median_is_retained_but_not_counted(self) -> None:
        path = self.write_pilot([self.note(0, 261.6, None)])
        report = load_pilot_pitch(path, "baseline")
        self.assertIsNone(report.median_absolute_cents())
        self.assertFalse(report.within_limits())


class DurationPlacementTests(unittest.TestCase):

    """V06 requires independent duration mapping, measured per placement."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def build(self, produced: int | None) -> Path:
        case = self.root / "case"
        write_case(case, 67, 48000, 60000,
                   [voiced(48000 + 256 * i, midi_hz(67)) for i in range(10)])
        diagnostics = json.loads((case / "diagnostics.json").read_text())
        placement = diagnostics["phrases"][0]["rendered_placements"][0]
        if produced is None:
            # A record that never stated its produced length must not read as exact.
            placement.pop("frame_count", None)
        else:
            placement["frame_count"] = produced
        (case / "diagnostics.json").write_text(json.dumps(diagnostics))
        return case

    def test_exact_span_is_within_the_frozen_limit(self) -> None:
        case = self.build(12000)
        summary = summarise_case(case, "case")
        self.assertEqual(1, len(summary.durations))
        self.assertEqual(0, summary.durations[0].duration_error)
        self.assertTrue(summary.duration_ok)
        record = json.loads(summary.to_json())
        self.assertTrue(record["duration"]["within_frozen_limit"])
        self.assertEqual(0, record["duration"]["threshold_frames"])

    def test_shorter_than_requested_is_measured_as_a_failure(self) -> None:
        case = self.build(11990)
        summary = summarise_case(case, "case")
        self.assertEqual(-10, summary.durations[0].duration_error)
        self.assertFalse(summary.duration_ok)

    def test_longer_than_requested_is_measured_as_a_failure(self) -> None:
        case = self.build(12007)
        summary = summarise_case(case, "case")
        self.assertEqual(7, summary.durations[0].duration_error)
        self.assertFalse(summary.duration_ok)

    def test_a_missing_frame_count_is_not_treated_as_a_pass(self) -> None:
        case = self.build(None)
        summary = summarise_case(case, "case")
        self.assertIsNone(summary.durations[0].duration_error)
        self.assertFalse(summary.duration_ok)


class RendererSubstitutionTests(unittest.TestCase):
    """A silent backend change is the hidden fallback V06 forbids."""
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_case(self, actual: str, used_fallback: bool, requested: str = "classic-psola") -> Path:
        case = self.root / "case"
        write_case(case, 67, 48000, 60000,
                   [voiced(48000 + 256 * i, midi_hz(67)) for i in range(10)])
        diagnostics = json.loads((case / "diagnostics.json").read_text())
        placement = diagnostics["phrases"][0]["rendered_placements"][0]
        placement["requested_renderer"] = requested
        placement["actual_renderer"] = actual
        placement["used_fallback"] = used_fallback
        (case / "diagnostics.json").write_text(json.dumps(diagnostics))
        return case

    def test_matching_renderer_is_not_a_substitution(self) -> None:
        case = self.write_case("classic-psola", False)
        summary = summarise_case(case, "case")
        self.assertEqual(1, summary.renderers.placements)
        self.assertEqual(0, summary.renderers.fallbacks)
        self.assertFalse(summary.renderers.has_hidden_fallback())

    def test_reported_fallback_is_truthful_not_hidden(self) -> None:
        case = self.write_case("raw", True)
        summary = summarise_case(case, "case")
        self.assertEqual(1, summary.renderers.fallbacks)
        self.assertEqual(1, len(summary.renderers.substitutions))
        self.assertFalse(summary.renderers.has_hidden_fallback())

    def test_unreported_renderer_change_is_a_hidden_fallback(self) -> None:
        case = self.write_case("raw", False)
        summary = summarise_case(case, "case")
        self.assertTrue(summary.renderers.has_hidden_fallback())
        hidden = summary.renderers.hidden_substitutions()
        self.assertEqual(1, len(hidden))
        self.assertEqual("classic-psola", hidden[0].requested_renderer)
        self.assertEqual("raw", hidden[0].actual_renderer)
        payload = json.loads(summary.to_json())
        self.assertTrue(payload["renderers"]["has_hidden_fallback"])


if __name__ == "__main__":
    unittest.main()
