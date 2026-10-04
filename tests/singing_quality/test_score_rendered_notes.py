"""The rendered-note scorer must not repeat the arithmetic that broke the last one."""

import unittest

from tools.singing_quality.score_rendered_notes import (
    frames_per_tick,
    midi_to_hz,
    score_note,
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


if __name__ == "__main__":
    unittest.main()
