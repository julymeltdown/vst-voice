from __future__ import annotations

import copy
import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "voicebank-script-generator" / "main.py"
SPEC = importlib.util.spec_from_file_location("seam_voicebank_script_generator", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class VoicebankScriptGeneratorTests(unittest.TestCase):
    def test_default_inventory_is_complete_and_hash_bound(self) -> None:
        inventory = generator.generate_inventory()
        self.assertEqual([], generator.validate_inventory(inventory))
        self.assertGreater(len(inventory["units"]), 100)
        self.assertEqual(2, len(inventory["pitchLayers"]))
        self.assertEqual(len(inventory["requiredCoverage"]), len(set(inventory["requiredCoverage"])))

    def test_same_profile_produces_byte_identical_json_and_csv(self) -> None:
        profile = {
            "profileId": "test-ja-v1",
            "vowels": ["a", "i"],
            "consonants": ["k", "m"],
            "specialPhones": ["br"],
            "includeKinds": ["sustain", "release", "cv", "vc", "vv"],
            "pitchLayers": [60, 72],
            "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
            "alternateTakes": 1,
        }
        first = generator.generate_inventory(profile)
        second = generator.generate_inventory(json.loads(generator.canonical_json(profile)))
        first_json = json.dumps(first, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
        second_json = json.dumps(second, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
        self.assertEqual(first_json.encode("utf-8"), second_json.encode("utf-8"))
        self.assertEqual(generator.render_operator_csv(first), generator.render_operator_csv(second))

    def test_missing_mandatory_transition_is_reported_exactly(self) -> None:
        inventory = generator.generate_inventory(
            {
                "profileId": "test-ja-v1",
                "vowels": ["a"],
                "consonants": ["k"],
                "specialPhones": ["br"],
                "includeKinds": ["sustain", "cv"],
                "pitchLayers": [60, 72],
                "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
                "alternateTakes": 1,
            }
        )
        broken = copy.deepcopy(inventory)
        broken["units"] = [unit for unit in broken["units"] if unit["coverageKey"] != "cv:k:a"]
        errors = generator.validate_inventory(broken)
        self.assertTrue(any("required coverage is missing: cv:k:a" in error for error in errors))

    def test_duplicate_alias_and_unsafe_filename_fail(self) -> None:
        profile = {
            "profileId": "test-ja-v1",
            "vowels": ["a", "a"],
            "consonants": ["k"],
            "specialPhones": ["br"],
            "includeKinds": ["sustain"],
            "pitchLayers": [60, 72],
            "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
            "alternateTakes": 1,
        }
        with self.assertRaisesRegex(ValueError, "duplicate aliases"):
            generator.generate_inventory(profile)
        inventory = generator.generate_inventory(
            {
                "profileId": "test-ja-v1",
                "vowels": ["a"],
                "consonants": ["k"],
                "specialPhones": ["br"],
                "includeKinds": ["sustain"],
                "pitchLayers": [60, 72],
                "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
                "alternateTakes": 1,
            }
        )
        inventory["units"][0]["filename"] = "../escape.wav"
        self.assertTrue(any("filename is unsafe" in error for error in generator.validate_inventory(inventory)))

    def test_two_layer_reduced_range_requires_explicit_range_result(self) -> None:
        profile = {
            "profileId": "test-ja-v1",
            "vowels": ["a"],
            "consonants": ["k"],
            "specialPhones": ["br"],
            "includeKinds": ["sustain"],
            "pitchLayers": [60, 72],
            "rangeTest": {"method": "", "minMidi": 60, "maxMidi": 72, "result": "NOT_RUN"},
            "alternateTakes": 1,
        }
        with self.assertRaisesRegex(ValueError, "rangeTest"):
            generator.generate_inventory(profile)

    def test_retake_group_must_be_assigned(self) -> None:
        inventory = generator.generate_inventory(
            {
                "profileId": "test-ja-v1",
                "vowels": ["a"],
                "consonants": ["k"],
                "specialPhones": ["br"],
                "includeKinds": ["sustain"],
                "pitchLayers": [60, 72],
                "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
                "alternateTakes": 1,
            }
        )
        inventory["units"][0]["retakeGroup"] = ""
        self.assertTrue(any("retakeGroup is required" in error for error in generator.validate_inventory(inventory)))

    def test_production_assignments_choose_one_planned_take_per_required_unit(self) -> None:
        inventory = generator.generate_inventory(
            {
                "profileId": "test-ja-v1",
                "vowels": ["a", "i"],
                "consonants": ["k"],
                "specialPhones": ["br"],
                "includeKinds": ["sustain", "cv"],
                "pitchLayers": [60, 72],
                "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
                "alternateTakes": 2,
            }
        )
        assignments = generator.production_assignments(inventory)
        self.assertEqual(
            len(inventory["requiredCoverage"]) * len(inventory["pitchLayers"]),
            len(assignments),
        )
        self.assertEqual(len(assignments), len({(item["coverageKey"], item["pitchLayer"]) for item in assignments}))
        self.assertTrue(all(item["promptId"] and item["plannedTakeId"] for item in assignments))
        self.assertTrue(all(item["state"] == "MISSING" for item in assignments))

    def test_inventory_validation_fails_closed_on_missing_pairs_and_bad_layers(self) -> None:
        inventory = generator.generate_inventory(
            {
                "profileId": "test-ja-validation-v1",
                "vowels": ["a"],
                "consonants": ["k"],
                "specialPhones": ["br"],
                "includeKinds": ["sustain"],
                "pitchLayers": [60, 72],
                "rangeTest": {
                    "method": "test-range",
                    "minMidi": 60,
                    "maxMidi": 72,
                    "result": "PASS",
                },
                "alternateTakes": 1,
            }
        )
        missing = copy.deepcopy(inventory)
        missing["units"] = [
            unit for unit in missing["units"] if unit["pitchLayer"] != 72
        ]
        self.assertTrue(
            any(
                "required coverage is missing: sustain:a at pitch layer 72"
                in error
                for error in generator.validate_inventory(missing)
            )
        )

        malformed = copy.deepcopy(inventory)
        malformed["pitchLayers"] = None
        self.assertTrue(
            any("pitchLayers" in error for error in generator.validate_inventory(malformed))
        )


if __name__ == "__main__":
    unittest.main()
