import copy
import unittest

from tools.voice_model_training.phrase_fingerprint import fingerprint, project_events, token_events


class PhraseFingerprintTests(unittest.TestCase):
    def test_transposition_and_time_scaling_keep_family_not_exact_score(self):
        events = token_events(["あ:60:480", "pau:60:240", "い:64:720"])
        changed = [dict(e, midi=None if e["midi"] is None else e["midi"] + 5,
                        startTick=e["startTick"] * 2, durationTick=e["durationTick"] * 2) for e in events]
        first, second = fingerprint(events), fingerprint(changed)
        self.assertNotEqual(first["scoreExactSha256"], second["scoreExactSha256"])
        self.assertEqual(first["scoreFamilySha256"], second["scoreFamilySha256"])
        self.assertEqual(first["lyricSequenceSha256"], second["lyricSequenceSha256"])

    def test_resolution_offset_and_unicode_normalization(self):
        events = [dict(language="en", lyric="é", midi=60, startTick=0, durationTick=480)]
        shifted = [dict(events[0], lyric="e\u0301", startTick=100, durationTick=960)]
        self.assertEqual(fingerprint(events), fingerprint(shifted, ppq=1920))

    def test_changed_lyrics_and_melody_are_distinguished(self):
        events = token_events(["あ:60:480", "い:64:720"])
        changed = copy.deepcopy(events)
        changed[1]["lyric"] = "う"
        self.assertNotEqual(fingerprint(events)["scoreFamilySha256"], fingerprint(changed)["scoreFamilySha256"])
        self.assertEqual(fingerprint(events)["melodyRhythmSha256"], fingerprint(changed)["melodyRhythmSha256"])
        changed[1]["midi"] = 67
        self.assertNotEqual(fingerprint(events)["melodyRhythmSha256"], fingerprint(changed)["melodyRhythmSha256"])

    def test_project_ids_do_not_disguise_reuse(self):
        project = dict(ppq=960, vocalTracks=[dict(regions=[dict(
            lyrics=[dict(id="a", language="ja", surface="あ"), dict(id="b", language="ja", surface="pau")],
            notes=[dict(id="n1", lyricId="a", midiKey=60, startTick=0, durationTick=480),
                   dict(id="n2", lyricId="b", midiKey=99, startTick=480, durationTick=240)])])])
        events = project_events(project)
        self.assertEqual(fingerprint(events), fingerprint(token_events(["あ:60:480", "pau:1:240"])))
        project["vocalTracks"][0]["regions"][0]["notes"][0]["id"] = "renamed"
        self.assertEqual(events, project_events(project))
        project["vocalTracks"][0]["regions"][0]["lyrics"].append(dict(id="a", language="ja", surface="い"))
        with self.assertRaises(ValueError):
            project_events(project)

    def test_invalid_or_ambiguous_geometry_refused(self):
        events = token_events(["あ:60:480", "い:64:720"])
        for change in (dict(startTick=0), dict(durationTick=0), dict(midi=True), dict(lyric="pau")):
            altered = copy.deepcopy(events)
            altered[1].update(change)
            with self.assertRaises(ValueError):
                fingerprint(altered)
        with self.assertRaises(ValueError):
            token_events(["pau:60:480"])
        with self.assertRaises(ValueError):
            fingerprint(events, ppq=True)

    def test_malformed_project_shapes_are_rejected(self):
        for project in (None, [], {}, {"vocalTracks": [None]},
                        {"vocalTracks": [{"regions": None}]},
                        {"vocalTracks": [{"regions": [None]}]},
                        {"vocalTracks": [{"regions": [{"lyrics": [None]}]}]}):
            with self.subTest(project=project), self.assertRaises(ValueError):
                project_events(project)


if __name__ == "__main__":
    unittest.main()
