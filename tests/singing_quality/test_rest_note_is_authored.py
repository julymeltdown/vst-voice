"""A silent note in a render must be provably an authored rest, not a pitch defect."""

import json
import unittest

from tools.singing_quality.verify_rest_note_is_authored import (
    midi_to_hz,
    note_span,
    rest_notes,
    segment_rms,
    strongest_partial,
    write_variant,
)

import numpy as np
import tempfile
import pathlib


def _project(hint, surface, midi=74):
    return {
        "vocalTracks": [{
            "regions": [{
                "lyrics": [{"id": "ly1", "language": "ja", "surface": surface}],
                "notes": [{
                    "id": "n1", "lyricId": "ly1", "midiKey": midi,
                    "startTick": 2640, "durationTick": 480, "phoneticHint": hint,
                }],
            }],
        }],
    }


class RestNoteIsAuthoredTests(unittest.TestCase):
    def test_pau_hint_marks_the_note_as_a_rest(self):
        found = rest_notes(_project("pau", "pau"))
        self.assertEqual(1, len(found))
        self.assertEqual(74, found[0]["midiKey"])

    def test_a_voiced_note_is_not_reported_as_a_rest(self):
        self.assertEqual([], rest_notes(_project(None, "\u3042")))

    def test_pau_surface_alone_marks_a_rest_even_without_the_hint(self):
        found = rest_notes(_project(None, "pau"))
        self.assertEqual(1, len(found))

    def test_variant_rewrites_only_the_target_note_and_lyric(self):
        project = _project("pau", "pau")
        note = rest_notes(project)[0]
        with tempfile.TemporaryDirectory() as scratch:
            destination = pathlib.Path(scratch) / "control"
            write_variant(project, note, destination)
            written = json.loads((destination / "project.seam").read_text())
        region = written["vocalTracks"][0]["regions"][0]
        self.assertIsNone(region["notes"][0]["phoneticHint"])
        self.assertEqual("\u3042", region["lyrics"][0]["surface"])
        # Pitch and timing are untouched: the control isolates the rest hint.
        self.assertEqual(74, region["notes"][0]["midiKey"])
        self.assertEqual(2640, region["notes"][0]["startTick"])
        self.assertEqual(480, region["notes"][0]["durationTick"])

    def test_note_span_covers_exactly_the_noted_frames(self):
        note = {"startTick": 2640, "durationTick": 480}
        start, end = note_span(note, 48000)
        # 120bpm at 960ppq == 1920 ticks/second == 25 frames/tick at 48kHz.
        self.assertEqual(2640 * 25, start)
        self.assertEqual((2640 + 480) * 25, end)

    def test_silence_and_pitch_are_distinguished_by_the_signal_itself(self):
        rate = 48000
        note = {"startTick": 2640, "durationTick": 480}
        start, end = note_span(note, rate)
        written = midi_to_hz(74)
        # Build a full-length buffer so the note's absolute span indexes real audio,
        # exactly as it does when slicing a rendered WAV.
        voiced = np.zeros(end)
        time = np.arange(start, end) / rate
        voiced[start:end] = np.sin(2 * np.pi * written * time)
        silence = np.zeros(end)
        self.assertLess(segment_rms(silence, rate, note), 0.002)
        self.assertGreater(segment_rms(voiced, rate, note), 0.002)
        # The strongest partial of the voiced note is the written pitch, so an
        # audible note 74 is a correct note 74 rather than an octave error.
        self.assertAlmostEqual(written, strongest_partial(voiced, rate, note), delta=written * 0.01)
        self.assertEqual(0.0, strongest_partial(silence, rate, note))


if __name__ == "__main__":
    unittest.main()
