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
        # The profile set is closed. A bare ternary treated every unrecognised value as the
        # five-second smoke duration while still recording the name the caller typed, so a
        # mistyped long soak produced a passing receipt without running. The runner must name
        # both supported profiles and refuse anything else.
        self.assertIn('profile == "full" ? 7200U : profile == "smoke" ? 5U : 0U', source)
        self.assertIn('unsupported --profile', source)
        self.assertIn('expected smoke or full', source)

    def test_unknown_profile_is_refused_rather_than_downgraded(self) -> None:
        """An unrecognised duration must fail, not silently become a smoke run."""
        import subprocess
        import tempfile

        runner = None
        for candidate in ("build/release/phase12c/seam_phase12c_soak",
                           "build/debug/phase12c/seam_phase12c_soak"):
            if (ROOT / candidate).is_file():
                runner = ROOT / candidate
                break
        if runner is None:
            self.skipTest("soak runner is not built in this configuration")
        with tempfile.TemporaryDirectory() as directory:
            for profile in ("ful", "full ", "FULL", "soak"):
                with self.subTest(profile=profile):
                    output = Path(directory) / "out.json"
                    result = subprocess.run(
                        [str(runner), "--profile", profile, "--output", str(output)],
                        capture_output=True, text=True, timeout=120)
                    self.assertEqual(result.returncode, 2, (profile, result.stdout, result.stderr))
                    self.assertIn("unsupported --profile", result.stderr)
                    # A refused run must not leave a receipt behind for a later step to read.
                    self.assertFalse(output.exists(), profile)
            # The supported smoke profile still runs, so the refusal is about the name, not the path.
            smoke = Path(directory) / "smoke.json"
            ran = subprocess.run([str(runner), "--profile", "smoke", "--output", str(smoke)],
                                 capture_output=True, text=True, timeout=180)
            self.assertEqual(ran.returncode, 0, (ran.stdout, ran.stderr))
            self.assertTrue(smoke.is_file())

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
