"""Portable score imports must publish the selected recipe, preserving user files."""

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "assets/pilots/seam-song-01/recipe.json"


class ImportRecipeCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="seam-import-recipe-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.selected = self.root / "source" / "recipe.json"
        self.selected.parent.mkdir()
        self.recipe = json.loads(RECIPE.read_text())
        # Deliberately noncanonical whitespace: publication must use the frozen
        # recipe that supplied the project's identity, not copy the source later.
        self.selected.write_text(json.dumps(self.recipe, indent=1) + "\n")
        body = (b"\x00\x90\x3c\x64\x00\xff\x05\x03" + "あ".encode()
                + b"\x83\x60\x80\x3c\x00\x00\xff\x2f\x00")
        self.midi = self.root / "source.mid"
        self.midi.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480)
                             + b"MTrk" + struct.pack(">I", len(body)) + body)
        self.ustx = self.root / "source.ustx"
        self.ustx.write_text(
            'ustx_version: "0.9"\nname: Recipe fixture\n'
            'time_signatures: [{bar_position: 0, beat_per_bar: 4, beat_unit: 4}]\n'
            'tempos: [{position: 0, bpm: 120}]\n'
            'tracks: [{singer: fixture, track_name: Lead, mute: false, solo: false, volume: 0, pan: 0}]\n'
            'voice_parts:\n  - name: Verse\n    track_no: 0\n'
            '    position: 0\n    duration: 480\n'
            '    notes:\n      - position: 0\n        duration: 480\n'
            '        tone: 60\n        lyric: "あ"\n'
            '        pitch: {data: [{x: 0, y: 0, shape: l}], snap_first: false}\n')

    def destinations(self, create_recipe_directory=True):
        for source in (self.midi, self.ustx):
            parent = self.root / source.suffix.lstrip(".")
            parent.mkdir()
            if create_recipe_directory:
                (parent / "recipes").mkdir()
            yield source, parent / "out.seam", parent / "recipes" / self.selected.name

    def import_score(self, source, target):
        return subprocess.run(
            [CLI, "import-score", str(source), str(target), "Recipe fixture",
             "--recipe", str(self.selected), "--language", "ja"],
            capture_output=True, text=True, timeout=15, check=False)

    def test_conflicting_recipe_is_refused_without_overwriting_user_material(self):
        other = dict(self.recipe, id="other-owned-singer")
        for source, target, saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                original = json.dumps(other, indent=2).encode()
                saved_recipe.write_bytes(original)
                result = self.import_score(source, target)
                self.assertEqual(5, result.returncode, result.stderr)
                self.assertIn("recipe", result.stderr.lower())
                self.assertFalse(target.exists())
                self.assertEqual(original, saved_recipe.read_bytes())

    def test_invalid_existing_recipe_is_refused_without_writing_project(self):
        for source, target, saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                saved_recipe.write_text("ORIGINAL USER MATERIAL\n")
                result = self.import_score(source, target)
                self.assertEqual(5, result.returncode, result.stderr)
                self.assertFalse(target.exists())
                self.assertEqual("ORIGINAL USER MATERIAL\n", saved_recipe.read_text())

    def test_same_singer_id_with_changed_parameters_is_refused(self):
        other = dict(self.recipe, phonation=dict(self.recipe["phonation"], aspiration=0.123))
        for source, target, saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                original = json.dumps(other).encode()
                saved_recipe.write_bytes(original)
                result = self.import_score(source, target)
                self.assertEqual(5, result.returncode, result.stderr)
                self.assertFalse(target.exists())
                self.assertEqual(original, saved_recipe.read_bytes())

    def test_matching_recipe_with_different_whitespace_is_reused_unchanged(self):
        for source, target, saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                original = json.dumps(self.recipe, indent=4).encode()
                saved_recipe.write_bytes(original)
                result = self.import_score(source, target)
                self.assertEqual(0, result.returncode, result.stderr)
                reference = json.loads(target.read_text())["vocalTracks"][0]["proceduralRecipe"]
                self.assertEqual(self.recipe["id"], reference["id"])
                self.assertEqual("recipes/recipe.json", reference["path"])
                self.assertEqual(original, saved_recipe.read_bytes())

    def test_recipe_directory_is_refused_without_writing_project(self):
        for source, target, saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                saved_recipe.mkdir()
                result = self.import_score(source, target)
                self.assertEqual(5, result.returncode, result.stderr)
                self.assertFalse(target.exists())
                self.assertTrue(saved_recipe.is_dir())

    @unittest.skipIf(os.name == "nt", "Windows symlink creation requires separate platform admission")
    def test_recipe_symlink_is_refused_even_when_it_points_to_selected_recipe(self):
        for source, target, saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                saved_recipe.symlink_to(self.selected)
                original = self.selected.read_bytes()
                result = self.import_score(source, target)
                self.assertEqual(5, result.returncode, result.stderr)
                self.assertFalse(target.exists())
                self.assertTrue(saved_recipe.is_symlink())
                self.assertEqual(original, self.selected.read_bytes())

    def test_new_recipe_bytes_match_the_frozen_project_identity(self):
        for source, target, saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                result = self.import_score(source, target)
                self.assertEqual(0, result.returncode, result.stderr)
                reference = json.loads(target.read_text())["vocalTracks"][0]["proceduralRecipe"]
                published = saved_recipe.read_bytes()
                self.assertEqual(self.recipe, json.loads(published))
                self.assertEqual(hashlib.sha256(published).hexdigest(), reference["contentHash"])
                self.assertEqual([], list(saved_recipe.parent.glob("*.tmp.*")))

    def test_existing_project_is_refused_before_recipe_publication(self):
        for source, target, saved_recipe in self.destinations(create_recipe_directory=False):
            with self.subTest(format=source.suffix):
                original = b"ORIGINAL USER PROJECT\n"
                target.write_bytes(original)
                result = self.import_score(source, target)
                self.assertEqual(6, result.returncode, result.stderr)
                self.assertEqual(original, target.read_bytes())
                self.assertFalse(saved_recipe.parent.exists())
                self.assertFalse(target.with_suffix(".seam.bak").exists())

    def test_unknown_options_and_multiple_names_are_refused_without_writes(self):
        tails = (["--languge", "ja"], ["--recpie", str(self.selected)],
                 ["First", "Second"], ["--unknown"])
        for source, target, saved_recipe in self.destinations(create_recipe_directory=False):
            for index, tail in enumerate(tails):
                with self.subTest(format=source.suffix, arguments=tail):
                    output = target.parent / f"invalid-{index}.seam"
                    result = subprocess.run(
                        [CLI, "import-score", str(source), str(output), *tail],
                        capture_output=True, text=True, timeout=15, check=False)
                    self.assertEqual(3, result.returncode, result.stderr)
                    self.assertTrue(result.stderr.strip())
                    self.assertFalse(output.exists())
                    self.assertFalse(saved_recipe.parent.exists())

    def test_project_name_may_follow_known_options(self):
        for source, target, _saved_recipe in self.destinations():
            with self.subTest(format=source.suffix):
                result = subprocess.run(
                    [CLI, "import-score", str(source), str(target),
                     "--language", "ja", "--recipe", str(self.selected), "Named after flags"],
                    capture_output=True, text=True, timeout=15, check=False)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual("Named after flags", json.loads(target.read_text())["name"])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_import_recipe_cli.py CLI")
    CLI = str(Path(sys.argv.pop()).resolve())
    unittest.main(verbosity=2)
