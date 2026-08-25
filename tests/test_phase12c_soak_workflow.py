import unittest
from pathlib import Path

from scripts.verify_phase12c_evidence import validate_soak_result


ROOT = Path(__file__).resolve().parents[1]


class Phase12CSoakWorkflowTests(unittest.TestCase):
    def test_workflow_runs_and_validates_the_exact_full_profile(self) -> None:
        workflow = (ROOT / ".github/workflows/phase12c-full-soak.yml").read_text()
        source = (ROOT / "phase12c/src/soak_runner.cpp").read_text()
        self.assertIn("workflow_dispatch", workflow)
        self.assertIn("--profile", workflow)
        self.assertIn("full", workflow)
        self.assertIn("--require-full", workflow)
        self.assertIn("verify_phase12c_evidence.py", workflow)
        self.assertIn("verify_phase12c_soak_packet.py", workflow)
        self.assertIn("runner.json", workflow)
        self.assertIn("soak-binary", workflow)
        self.assertIn("packet.json", workflow)
        self.assertIn("upload-artifact", workflow)
        self.assertIn('profile == "full" ? 7200U : 5U', source)

    def test_full_soak_validator_rejects_short_or_wrong_profile_records(self) -> None:
        passing = {
            "profile": "full",
            "requiredSeconds": 7200,
            "elapsedSeconds": 7200,
            "blocks": 1,
            "eventBlocks": 1,
            "resourcePublishes": 1,
            "resourceClears": 1,
            "maxActiveVoices": 32,
            "absoluteEnergy": 2.0,
            "peak": 0.5,
            "finite": True,
            "noteOns": 1,
            "noteOffs": 1,
            "steals": 1,
            "transitionHits": 1,
            "transitionFallbacks": 0,
            "midiEvents": 1,
            "expressionEvents": 1,
            "renderedFrames": 64,
            "silentFramesNoResource": 0,
            "eventOverflows": 0,
            "result": "PASS",
        }
        self.assertEqual(validate_soak_result(passing, True), [])
        short = dict(passing, elapsedSeconds=7199)
        self.assertTrue(validate_soak_result(short, True))
        wrong_profile = dict(passing, profile="smoke")
        self.assertTrue(validate_soak_result(wrong_profile, True))

    def test_soak_validator_requires_event_and_resource_workload_fields(self) -> None:
        passing = {
            "profile": "smoke",
            "requiredSeconds": 5,
            "elapsedSeconds": 5,
            "blocks": 1,
            "eventBlocks": 1,
            "resourcePublishes": 1,
            "resourceClears": 1,
            "maxActiveVoices": 1,
            "absoluteEnergy": 2.0,
            "peak": 0.5,
            "finite": True,
            "noteOns": 1,
            "noteOffs": 1,
            "steals": 1,
            "transitionHits": 1,
            "transitionFallbacks": 0,
            "midiEvents": 1,
            "expressionEvents": 1,
            "renderedFrames": 64,
            "silentFramesNoResource": 0,
            "eventOverflows": 0,
            "result": "PASS",
        }
        self.assertEqual(validate_soak_result(passing, False), [])
        incomplete = dict(passing)
        del incomplete["eventBlocks"]
        self.assertTrue(validate_soak_result(incomplete, False))

    def test_soak_validator_rejects_full_profile_with_smoke_duration(self) -> None:
        passing = {
            "profile": "smoke",
            "requiredSeconds": 5,
            "elapsedSeconds": 5,
            "blocks": 1,
            "eventBlocks": 1,
            "resourcePublishes": 1,
            "resourceClears": 1,
            "maxActiveVoices": 1,
            "absoluteEnergy": 2.0,
            "peak": 0.5,
            "finite": True,
            "noteOns": 1,
            "noteOffs": 1,
            "steals": 1,
            "transitionHits": 1,
            "transitionFallbacks": 0,
            "midiEvents": 1,
            "expressionEvents": 1,
            "renderedFrames": 64,
            "silentFramesNoResource": 0,
            "eventOverflows": 0,
            "result": "PASS",
        }
        self.assertTrue(validate_soak_result(dict(passing, profile="full"), False))

    def test_soak_validator_rejects_non_string_profile_without_crashing(self) -> None:
        errors = validate_soak_result({"profile": []}, False)
        self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main()
