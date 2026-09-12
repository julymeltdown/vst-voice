import copy
import unittest
import json
import subprocess
import sys
from pathlib import Path

from tools.voicebank_script_generator.draft_inventory import (
    draft_production_assignments, generate_draft_inventory,
    normalize_draft_profile, validate_draft_inventory,
)
from tools.voicebank_script_generator import generate_inventory, validate_inventory


class DraftInventoryTests(unittest.TestCase):
    def test_assignment_digest_matches_cpp_identity_contract(self):
        # These same vectors are asserted by test_production_style_identity.cpp.
        for style, expected in (
            ("neutral", "c6eea096f83ec23ee63a29980504370c9c18da78e9312e56641ce5ce77be91a8"),
            ('柔らかい "A"', "35700829e14437ae6c67c43592a6517ea547811148bf31f460a256c42e42a17a"),
        ):
            value = generate_draft_inventory({**self.profile(), "supportedStyles": [style]})
            rows = [r for r in value["units"] if r["coverageKey"] == "sustain:a" and r["pitchLayer"] == 60]
            self.assertEqual(2, len(rows))
            self.assertTrue(all(r["assignmentId"] == expected for r in rows))

    def test_real_cli_draft_does_not_inherit_range_pass(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run([sys.executable, str(root / "tools/voicebank-script-generator/main.py"),
                                 "--draft", "--profile", str(root / "assets/pilots/seam-pilot-01/profile.json")],
                                cwd=root, capture_output=True, text=True, check=True)
        value = json.loads(result.stdout)
        self.assertEqual([], validate_draft_inventory(value))
        self.assertEqual({"status": "NOT_ASSESSED"}, value["rangeAssessment"])
        missing = subprocess.run([sys.executable, str(root / "tools/voicebank-script-generator/main.py"), "--draft"],
                                 cwd=root, capture_output=True, text=True)
        self.assertNotEqual(0, missing.returncode)
        self.assertIn("explicit --profile", missing.stderr)

    def profile(self):
        return {"profileId": "pilot-ja", "vowels": ["a", "i"], "consonants": ["m", "k"],
                "specialPhones": ["N"], "includeKinds": ["sustain", "cv"],
                "supportedStyles": ["neutral", "soft"], "alternateTakes": 2}

    def test_styles_layers_and_alternates_have_distinct_ownership(self):
        value = generate_draft_inventory(self.profile())
        assignments = draft_production_assignments(value)
        self.assertEqual(36, len(assignments))
        self.assertEqual(72, len(value["units"]))
        self.assertEqual(36, len({(r["language"], r["style"], r["coverageKey"], r["pitchLayer"])
                                 for r in assignments}))
        self.assertTrue(all(r["state"] == "MISSING" and not r["markerReviewed"] for r in assignments))
        self.assertEqual({"status": "NOT_ASSESSED"}, value["rangeAssessment"])
        self.assertNotIn("rangeTest", value)
        self.assertEqual(value, generate_draft_inventory(self.profile()))
        self.assertEqual([], validate_draft_inventory(value))

    def test_colliding_style_slugs_do_not_collide_files_or_takes(self):
        profile = {**self.profile(), "supportedStyles": ["soft bright", "soft-bright"]}
        units = generate_draft_inventory(profile)["units"]
        for field in ("filename", "takeId", "promptId"):
            self.assertEqual(len(units), len({u[field] for u in units}))

    def test_long_symbols_keep_producer_ids_and_basenames_bounded(self):
        value = generate_draft_inventory({**self.profile(), "vowels": ["a" * 128], "consonants": ["k" * 128]})
        for unit in value["units"]:
            self.assertLessEqual(len(unit["promptId"].encode()), 128)
            self.assertLessEqual(len(unit["takeId"].encode()), 128)
            self.assertLessEqual(len(Path(unit["filename"]).name.encode()), 255)

    def test_tampering_cannot_claim_coverage_or_qualification(self):
        value = generate_draft_inventory(self.profile())
        for mutate in (
            lambda v: v["units"].pop(),
            lambda v: v["units"][0].update(style="soft"),
            lambda v: v["units"][0].update(filename="../outside.wav"),
            lambda v: v["units"][0].update(sessionBlock=True),
            lambda v: v["units"][0].update(pitchLayer=60.0),
            lambda v: v.update(rangeAssessment={"status": "PASS"}),
            lambda v: v.update(unknown=True),
        ):
            changed = copy.deepcopy(value)
            mutate(changed)
            self.assertTrue(validate_draft_inventory(changed))
            with self.assertRaises(ValueError):
                draft_production_assignments(changed)

    def test_invalid_profiles_fail_before_expanding_rows(self):
        for change in ({"rangeTest": {"result": "PASS"}}, {"pitchLayers": [True, 72]},
                       {"pitchLayers": [59, 72]}, {"language": "en"},
                       {"supportedStyles": ["soft", "soft"]}, {"alternateTakes": 999},
                       {"profileId": "bad\ud800"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                normalize_draft_profile({**self.profile(), **change})

    def test_legacy_reader_does_not_accept_style_owned_data(self):
        from tools.external_beta._production_workspace import prepare_production_draft_definition
        legacy = generate_inventory()
        self.assertEqual(1, legacy["schemaVersion"])
        self.assertEqual([], validate_inventory(legacy))
        self.assertTrue(validate_inventory(generate_draft_inventory(self.profile())))
        definition = prepare_production_draft_definition(generate_draft_inventory(self.profile()), None,
                                                        project_id="pilot", operator_id="producer")
        self.assertEqual(4, definition["schemaVersion"])
        self.assertEqual("ja", definition["language"])
        self.assertEqual({"neutral", "soft"}, {row["style"] for row in definition["unitAssignments"]})

    def test_malformed_inputs_return_errors(self):
        for value in (None, [], {}, {"profileId": []}):
            self.assertTrue(validate_draft_inventory(value))
