"""The rendered-note scorer must not repeat the arithmetic that broke the last one."""

import unittest
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

from tools.singing_quality.score_rendered_notes import (
    frames_per_tick,
    midi_to_hz,
    score_note,
    main,
)


def _project(ppq=960, bpm=120.0):
    return {"ppq": ppq, "tempoMap": [{"tick": 0, "bpm": bpm}]}


class ScoreRenderedNotesTests(unittest.TestCase):
    def test_frames_per_tick_is_not_the_reciprocal(self):
        # 48000 / (960 * 120/60) == 25 frames per tick. The earlier hand-written
        # scorer computed the reciprocal (0.0417), which made every analysis
        # window half a hop wide and produced confident octave errors.
        self.assertAlmostEqual(25.0, frames_per_tick(_project(), 48000), places=9)

    def test_frames_per_tick_scales_with_sample_rate(self):
        self.assertAlmostEqual(50.0, frames_per_tick(_project(), 96000), places=9)

    def test_frames_per_tick_tracks_tempo_and_ppq(self):
        self.assertAlmostEqual(50.0, frames_per_tick(_project(ppq=480), 48000), places=9)
        self.assertAlmostEqual(12.5, frames_per_tick(_project(bpm=240.0), 48000), places=9)

    def test_a_real_note_span_yields_many_frames_not_one(self):
        # A 960-tick note at 25 frames per tick is 24000 frames, so the middle
        # third is 8000 frames, which at a 256-frame hop is 31 samples. One or
        # two frames is the signature of the inverted arithmetic.
        hop = 256
        frames = [{"voiced": True, "f0Hz": 261.63} for _ in range(96000)]
        median, count = score_note(frames, hop, 0.0, 24000.0)
        self.assertIsNotNone(median)
        self.assertGreater(count, 30)

    def test_unvoiced_frames_are_excluded_from_the_median(self):
        hop = 256
        frames = []
        for index in range(400):
            frames.append({"voiced": index % 2 == 0, "f0Hz": 100.0 if index % 2 == 0 else 0.0})
        median, count = score_note(frames, hop, 0.0, 400 * hop)
        # Only the middle third is sampled: frames 133..266, half of which are
        # voiced.
        self.assertEqual(66, count)
        self.assertAlmostEqual(100.0, median, places=9)

    def test_a_silent_note_reports_no_measurement_rather_than_zero(self):
        hop = 256
        frames = [{"voiced": False, "f0Hz": 0.0} for _ in range(1000)]
        median, count = score_note(frames, hop, 0.0, 1000 * hop)
        self.assertIsNone(median)
        self.assertEqual(0, count)

    def test_midi_to_hz_matches_the_standard_reference(self):
        self.assertAlmostEqual(440.0, midi_to_hz(69), places=9)
        self.assertAlmostEqual(261.6255653005986, midi_to_hz(60), places=6)
        self.assertAlmostEqual(880.0, midi_to_hz(81), places=9)

    def test_source_frame_positions_are_used_instead_of_list_offsets(self):
        frames = [{"sourceFrame": 0, "voiced": True, "f0Hz": 220.0},
                  {"sourceFrame": 2048, "voiced": True, "f0Hz": 440.0},
                  {"sourceFrame": 5000, "voiced": True, "f0Hz": 880.0}]
        median, count = score_note(frames, 256, 0, 6000)
        self.assertEqual((440.0, 1), (median, count))

    def test_analysis_window_centres_select_the_note_span(self):
        frames = [{"sourceFrame": 0, "voiced": True, "f0Hz": 220.0},
                  {"sourceFrame": 256, "voiced": True, "f0Hz": 660.0},
                  {"sourceFrame": 512, "voiced": True, "f0Hz": 880.0}]
        self.assertEqual((440.0, 2), score_note(frames, 256, 0, 2048, window=2048))

    def run_project(self, project, pitch_at, rate=48000, extra=(), native=False,
                    feature_change=None, report_alias=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wav = root / "track.wav"
            wav.write_bytes(b"mocked feature source; not a native audio fixture")
            path = root / "project.seam"
            path.write_text(json.dumps(project))
            extent = rate * 5
            features = {"formatId": "com.project-seam.training-pitch-features", "schemaVersion": 1,
                        "sourceSha256": hashlib.sha256(wav.read_bytes()).hexdigest(),
                        "sampleRate": rate, "frameCount": extent, "windowFrames": 2048,
                        "hopSize": 256, "minimumHz": 60, "maximumHz": 1200,
                        "algorithm": "fft-autocorrelation-v3", "coverage": "full-hop-zero-padded",
                        "pitchFrames": [{"sourceFrame": source, "voiced": True,
                                         "f0Hz": pitch_at((source + 1024) / rate), "confidence": 1.0}
                                        for source in range(0, extent, 256)]}
            if native:
                features = {"formatId": "com.project-seam.voicebank-analysis", "schemaVersion": 2,
                            "sourceSha256": features["sourceSha256"], "sampleRate": rate,
                            "channels": 2, "frames": extent,
                            "pitchAnalysis": {"algorithm": "direct-autocorrelation-v3", "coverage": "CompleteWindows",
                                              "windowFrames": 2048, "hopFrames": 256, "minimumHz": 60, "maximumHz": 1200},
                            "pitchFrames": features["pitchFrames"][:len(range(0, extent - 2048 + 1, 256))]}
            if feature_change:
                feature_change(features)
            analysis = root / "analysis.json"
            analysis.write_text(json.dumps(features))
            mode = ["--audio", str(wav), "--analysis-json", str(analysis)] if native else ["--audio-dir", str(root), "--cli", "unused-mocked-native-cli"]
            if report_alias:
                extra = (*extra, "--report", str(path))
            original = path.read_bytes()
            with patch("tools.singing_quality.score_rendered_notes.extract_features", return_value=features), redirect_stdout(io.StringIO()):
                result = main(["--project", str(path), *mode, *extra])
            self.assertEqual(original, path.read_bytes())
            return result

    @staticmethod
    def placed_project(regions):
        return {"ppq": 960, "tempoMap": [{"tick": 0, "bpm": 120}],
                "settings": {"hostStartOffsetTick": 0, "bounceTimingAuthority": "fixed-audio"},
                "vocalTracks": [{"id": "10", "regions": regions}], "audioTracks": []}

    def test_region_placement_changes_the_measured_window(self):
        project = self.placed_project([{"startTick": 1920, "notes": [
            {"id": "20", "startTick": 0, "durationTick": 960, "midiKey": 69}]}])
        self.assertEqual(0, self.run_project(project, lambda second: 440.0 if second >= 1 else 220.0))

    def test_later_regions_cannot_be_silently_skipped(self):
        project = self.placed_project([
            {"startTick": 0, "notes": [{"id": "20", "startTick": 0, "durationTick": 960, "midiKey": 69}]},
            {"startTick": 1920, "notes": [{"id": "21", "startTick": 0, "durationTick": 960, "midiKey": 81}]}])
        self.assertEqual(1, self.run_project(project, lambda second: 440.0))

    def test_tempo_change_changes_absolute_note_time(self):
        project = self.placed_project([{"startTick": 0, "notes": [
            {"id": "20", "startTick": 1920, "durationTick": 960, "midiKey": 69}]}])
        project["tempoMap"].append({"tick": 960, "bpm": 60})
        self.assertEqual(0, self.run_project(project, lambda second: 440.0 if second >= 1.5 else 220.0))

    def test_explicit_sample_rate_mismatch_is_refused(self):
        project = self.placed_project([{"startTick": 0, "notes": [
            {"id": "20", "startTick": 0, "durationTick": 960, "midiKey": 69}]}])
        self.assertEqual(2, self.run_project(project, lambda second: 440.0,
                                           rate=44100, extra=("--sample-rate", "48000")))

    def simple_project(self):
        return self.placed_project([{"startTick": 0, "notes": [
            {"id": "20", "startTick": 0, "durationTick": 960, "midiKey": 69}]}])

    def test_native_stereo_analysis_is_accepted_without_mono_extraction(self):
        self.assertEqual(0, self.run_project(self.simple_project(), lambda second: 440.0, native=True))

    def test_native_source_hash_mismatch_is_refused(self):
        self.assertEqual(2, self.run_project(self.simple_project(), lambda second: 440.0, native=True,
                                           feature_change=lambda f: f.update(sourceSha256="0" * 64)))

    def test_missing_native_pitch_frame_is_refused(self):
        self.assertEqual(2, self.run_project(self.simple_project(), lambda second: 440.0, native=True,
                                           feature_change=lambda f: f["pitchFrames"].pop()))

    def test_reordered_native_source_positions_are_refused(self):
        self.assertEqual(2, self.run_project(self.simple_project(), lambda second: 440.0, native=True,
                                           feature_change=lambda f: f["pitchFrames"][2].update(sourceFrame=256)))

    def test_nonfinite_pitch_is_refused(self):
        self.assertEqual(2, self.run_project(self.simple_project(), lambda second: float("nan")))

    def test_low_confidence_does_not_count_as_pitch_accuracy(self):
        def low_confidence(features):
            for frame in features["pitchFrames"]:
                frame["confidence"] = 0.49
        self.assertEqual(1, self.run_project(self.simple_project(), lambda second: 440.0,
                                           feature_change=low_confidence))

    def test_ceiling_saturated_pitch_does_not_count_as_accuracy(self):
        self.assertEqual(1, self.run_project(self.simple_project(), lambda second: 1200.0))

    def test_nonzero_host_offset_is_added_in_seconds_after_tempo_projection(self):
        project = self.simple_project()
        project["tempoMap"].append({"tick": 960, "bpm": 60})
        project["settings"]["hostStartOffsetTick"] = 1920
        self.assertEqual(0, self.run_project(project, lambda second: 440.0 if second >= 1.5 else 220.0))

    def test_render_shift_occurs_before_tempo_projection(self):
        project = self.simple_project()
        project["tempoMap"].append({"tick": 960, "bpm": 60})
        self.assertEqual(0, self.run_project(project, lambda second: 440.0 if second >= 1.5 else 220.0,
                                           extra=("--render-offset-ticks", "1920")))

    def test_follow_host_document_tempo_is_refused(self):
        project = self.simple_project()
        project["settings"]["bounceTimingAuthority"] = "follow-host"
        self.assertEqual(2, self.run_project(project, lambda second: 440.0))

    def test_polyphonic_master_is_refused(self):
        project = self.simple_project()
        project["vocalTracks"].append(dict(project["vocalTracks"][0]))
        self.assertEqual(2, self.run_project(project, lambda second: 440.0, native=True))

    def test_master_with_backing_audio_is_refused(self):
        project = self.simple_project()
        project["audioTracks"] = [{"id": "11"}]
        self.assertEqual(2, self.run_project(project, lambda second: 440.0, native=True))

    def test_missing_stem_is_refused_instead_of_partial_success(self):
        project = self.simple_project()
        project["vocalTracks"].append(dict(project["vocalTracks"][0]))
        self.assertEqual(2, self.run_project(project, lambda second: 440.0))

    def test_overlapping_regions_are_refused(self):
        project = self.simple_project()
        project["vocalTracks"][0]["regions"].append(dict(project["vocalTracks"][0]["regions"][0]))
        self.assertEqual(2, self.run_project(project, lambda second: 440.0))

    def test_truncated_audio_cannot_pass_a_partial_note(self):
        project = self.simple_project()
        project["vocalTracks"][0]["regions"][0]["notes"][0]["durationTick"] = 960 * 11
        self.assertEqual(2, self.run_project(project, lambda second: 440.0))

    def test_report_cannot_overwrite_project(self):
        self.assertEqual(2, self.run_project(self.simple_project(), lambda second: 440.0, report_alias=True))


if __name__ == "__main__":
    unittest.main()
