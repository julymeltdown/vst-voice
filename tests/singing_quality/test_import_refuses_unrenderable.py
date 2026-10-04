"""A DAW import that cannot render must not leave a dead-end project on disk.

The codec layer deliberately only warns: a caller may want the draft so the
lyrics can be corrected in the editor. The command line is where that choice is
made, so the command is where the refusal belongs -- and only when the caller
asked for a recipe, because asking for one means intending to render.

Skipped when the CLI or a recipe is unavailable, so the unit suite stays usable
without a build tree.
"""

import json
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile
import unittest

CLI = os.environ.get("SEAM_VOICEBANK_CLI", "build/release/seam_voicebank_cli")
RECIPE_GLOB = os.environ.get("SEAM_TEST_RECIPE_GLOB", "")
PPQ = 480


def _vlq(value):
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def _track(events):
    body = bytearray()
    for delta, data in events:
        body += _vlq(delta) + data
    body += _vlq(0) + b"\xff\x2f\x00"
    return b"MTrk" + len(body).to_bytes(4, "big") + bytes(body)


def _midi_with_lyrics(path, lyrics):
    """One track, two notes, each with a lyric at the note's onset."""
    events = []
    for index, (pitch, text) in enumerate([(60, lyrics[0]), (62, lyrics[1])]):
        tick = index * PPQ
        events.append((tick, b"\x90" + bytes([pitch, 100])))
        events.append((tick + PPQ, b"\x80" + bytes([pitch, 0])))
        raw = text.encode("utf-8")
        events.append((tick, b"\xff\x05" + bytes([len(raw)]) + raw))
    events.sort(key=lambda item: item[0])
    body = []
    previous = 0
    for tick, data in events:
        body.append((tick - previous, data))
        previous = tick
    header = (b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big")
              + (1).to_bytes(2, "big") + PPQ.to_bytes(2, "big"))
    path.write_bytes(header + _track(body))


def _find_recipe():
    if not RECIPE_GLOB:
        return None
    matches = sorted(pathlib.Path("/").glob(RECIPE_GLOB.lstrip("/")))
    return matches[0] if matches else None


@unittest.skipUnless(pathlib.Path(CLI).exists(), "seam_voicebank_cli is not built")
class ImportRefusesUnrenderableTests(unittest.TestCase):
    def setUp(self):
        self.work = pathlib.Path(tempfile.mkdtemp(prefix="seam-import-gap-"))
        self.recipe = _find_recipe()

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    @unittest.skipUnless(_find_recipe(), "no recipe available for the render check")
    def test_romaji_with_recipe_is_refused_and_writes_nothing(self):
        midi = self.work / "romaji.mid"
        _midi_with_lyrics(midi, ["la", "ti"])
        target = self.work / "out.seam"
        result = subprocess.run(
            [CLI, "import-score", str(midi), str(target), "Romaji",
             "--recipe", str(self.recipe), "--language", "ja"],
            capture_output=True, text=True, check=False)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("would not render", result.stderr)
        self.assertIn("--language", result.stderr)
        # The whole point: nothing on disk that looks like a successful import.
        self.assertFalse(target.exists())

    @unittest.skipUnless(_find_recipe(), "no recipe available for the render check")
    def test_romaji_without_recipe_still_yields_an_editable_draft(self):
        midi = self.work / "romaji2.mid"
        _midi_with_lyrics(midi, ["la", "ti"])
        target = self.work / "draft.seam"
        result = subprocess.run(
            [CLI, "import-score", str(midi), str(target), "Draft"],
            capture_output=True, text=True, check=False)
        self.assertEqual(0, result.returncode)
        self.assertTrue(target.exists())
        project = json.loads(target.read_text())
        self.assertEqual(2, len(project["vocalTracks"][0]["regions"][0]["notes"]))

    @unittest.skipUnless(_find_recipe(), "no recipe available for the render check")
    def test_kana_with_recipe_imports_cleanly(self):
        midi = self.work / "kana.mid"
        _midi_with_lyrics(midi, ["\u3042", "\u3044"])
        target = self.work / "kana.seam"
        result = subprocess.run(
            [CLI, "import-score", str(midi), str(target), "Kana",
             "--recipe", str(self.recipe), "--language", "ja"],
            capture_output=True, text=True, check=False)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertTrue(target.exists())
        # The recipe must travel with the project, not point somewhere else.
        recipe = project_recipe_path(target)
        self.assertTrue(recipe.startswith("recipes/"), recipe)
        self.assertTrue((target.parent / recipe).is_file())


def project_recipe_path(project_path):
    project = json.loads(project_path.read_text())
    reference = project["vocalTracks"][0].get("proceduralRecipe")
    return reference["path"] if reference else ""


if __name__ == "__main__":
    unittest.main()
