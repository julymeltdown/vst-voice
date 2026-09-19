"""The corpus generator must produce a configuration prepare_corpus accepts."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.generate_procedural_corpus import (
    MAXIMUM_TICKS, MINIMUM_TICKS, SCALE, SYLLABLES, _phrase, _total_ticks, generate,
)
from tools.voice_model_training.prepare_corpus import load_corpus_config


def fake_pilot(root: Path, script: Path) -> Path:
    """A stand-in pilot that lays out the committed export shape it must produce."""
    script.write_text(
        "import json, pathlib, sys\n"
        "root = pathlib.Path(sys.argv[1])\n"
        "take = root / 'baseline'\n"
        "(take / 'candidates').mkdir(parents=True)\n"
        "name = '0000000000000001-0000000000000002'\n"
        "(take / 'candidates' / (name + '.json')).write_bytes(json.dumps({'frameCount': 2048}).encode())\n"
        "(take / 'candidates' / (name + '.wav')).write_bytes(b'RIFF')\n"
        "(take / 'receipt.json').write_bytes(json.dumps({'state': 'COMMITTED'}).encode())\n")
    return script


class ProceduralCorpusTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.pilot = fake_pilot(self.root, self.root / "pilot.py")
        os.chmod(self.pilot, 0o755)

    def test_phrases_are_deterministic_and_inside_the_engine_bounds(self):
        first = _phrase("seed-a", 7, 16)
        self.assertEqual(first, _phrase("seed-a", 7, 16))
        self.assertNotEqual(first, _phrase("seed-a", 8, 16))
        self.assertNotEqual(first, _phrase("seed-b", 7, 16))
        self.assertEqual(len(first), 16)
        for token in first:
            syllable, midi, duration = token.split(":")
            self.assertIn(syllable, SYLLABLES)
            self.assertIn(int(midi), SCALE)
            # The shortest duration must leave room for the engine's widest
            # articulation, which is why no duration falls below one quarter note.
            self.assertGreaterEqual(int(duration), MINIMUM_TICKS)
        self.assertLessEqual(_total_ticks(first), MAXIMUM_TICKS)
        self.assertGreaterEqual(_total_ticks(first), MINIMUM_TICKS)
        # Every phrase must stay within the duration bound the pilot enforces.
        for index in range(200):
            with self.subTest(index=index):
                self.assertLessEqual(_total_ticks(_phrase("s", index, 16)), MAXIMUM_TICKS)

    def test_generation_writes_a_configuration_prepare_corpus_accepts(self):
        output = self.root / "corpus"
        # The fake pilot is a Python script, so invoke it through the interpreter.
        with patch("tools.voice_model_training.generate_procedural_corpus.subprocess.run") as run:
            # These tests mock the pilot deliberately: they check the generator's own
            # contract against the documented export shape. The real pilot is run
            # separately as evidence, so a shape drift shows up there.
            def lay_out(command, **kwargs):
                import subprocess as real
                root = Path(command[1])
                take = root / "baseline"
                (take / "candidates").mkdir(parents=True, exist_ok=True)
                name = "0000000000000001-0000000000000002"
                (take / "candidates" / (name + ".json")).write_bytes(b"{}")
                (take / "candidates" / (name + ".wav")).write_bytes(b"RIFF")
                (take / "receipt.json").write_bytes(b'{"state":"COMMITTED"}')
                return real.CompletedProcess(command, 0, "", "")
            run.side_effect = lay_out
            result = generate(pilot=self.pilot, output=output, count=4, seed="unit-seed",
                              extractor="/tmp/extractor", training_scopes=["sourceUse", "modelTraining"],
                              notes=8)
        self.assertEqual(result["songs"], 4)
        self.assertEqual(len(result["heldOutSongIds"]), 1)
        # The produced configuration must pass the corpus loader that will consume it.
        path = Path(result["sourcesPath"])
        loaded = load_corpus_config(path, result["sourcesSha256"])
        self.assertEqual(len(loaded["songs"]), 4)
        self.assertEqual(loaded["trainingScopes"], ["modelTraining", "sourceUse"])
        self.assertEqual(loaded["extractor"], "/tmp/extractor")
        self.assertIn(loaded["heldOutSongIds"][0], {s["songId"] for s in loaded["songs"]})
        for song in loaded["songs"]:
            with self.subTest(song=song["sourceId"]):
                self.assertEqual(song["songId"], song["sourceId"])
                self.assertTrue((Path(song["exportRoot"]) / "receipt.json").is_file())
                self.assertTrue(song["candidatePath"].startswith("candidates/"))

    def test_held_out_song_is_chosen_by_index_not_by_outcome(self):
        with patch("tools.voice_model_training.generate_procedural_corpus.subprocess.run") as run:
            def lay_out(command, **kwargs):
                import subprocess as real
                take = Path(command[1]) / "baseline"
                (take / "candidates").mkdir(parents=True, exist_ok=True)
                name = "0000000000000001-0000000000000002"
                (take / "candidates" / (name + ".json")).write_bytes(b"{}")
                (take / "candidates" / (name + ".wav")).write_bytes(b"RIFF")
                (take / "receipt.json").write_bytes(b'{"state":"COMMITTED"}')
                return real.CompletedProcess(command, 0, "", "")
            run.side_effect = lay_out
            first = generate(pilot=self.pilot, output=self.root / "a", count=3, seed="s",
                             extractor="x", training_scopes=["sourceUse"], notes=6)
            second = generate(pilot=self.pilot, output=self.root / "b", count=3, seed="s",
                              extractor="x", training_scopes=["sourceUse"], notes=6)
        self.assertEqual(first["heldOutSongIds"], second["heldOutSongIds"])
        self.assertEqual(first["heldOutSongIds"], ["procedural-song-00002"])

    def test_failures_and_invalid_arguments_are_refused(self):
        with self.assertRaises(ValueError):
            generate(pilot=self.root / "absent", output=self.root / "x", count=1, seed="s",
                     extractor="x", training_scopes=["sourceUse"])
        for changes in (dict(count=0), dict(count=5001), dict(seed=""), dict(notes=0), dict(notes=65),
                        dict(start=-1), dict(extractor=""), dict(training_scopes=[]),
                        dict(training_scopes=["invented"]), dict(training_scopes=["sourceUse", "sourceUse"])):
            arguments = dict(pilot=self.pilot, output=self.root / f"bad-{abs(hash(str(changes)))}",
                             count=1, seed="s", extractor="x", training_scopes=["sourceUse"], notes=8)
            arguments.update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                generate(**arguments)

    def test_a_failed_render_stops_rather_than_publishing_a_partial_corpus(self):
        with patch("tools.voice_model_training.generate_procedural_corpus.subprocess.run") as run:
            import subprocess as real
            run.return_value = real.CompletedProcess([], 1, "", "pilot refused the phrase")
            with self.assertRaises(ValueError):
                generate(pilot=self.pilot, output=self.root / "failed", count=2, seed="s",
                         extractor="x", training_scopes=["sourceUse"], notes=8)
        self.assertFalse((self.root / "failed" / "corpus-sources.json").exists())

    def test_existing_output_and_non_executable_pilot_are_refused(self):
        output = self.root / "existing"
        output.mkdir()
        with self.assertRaises(ValueError):
            generate(pilot=self.pilot, output=output, count=1, seed="s", extractor="x",
                     training_scopes=["sourceUse"])
        plain = self.root / "plain.txt"
        plain.write_text("not executable")
        os.chmod(plain, 0o644)
        with self.assertRaises(ValueError):
            generate(pilot=plain, output=self.root / "y", count=1, seed="s", extractor="x",
                     training_scopes=["sourceUse"])


if __name__ == "__main__":
    unittest.main()
