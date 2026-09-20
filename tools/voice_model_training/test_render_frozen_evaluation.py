import copy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.phrase_fingerprint import fingerprint, token_events
from tools.voice_model_training.render_frozen_evaluation import render, validate_plan


class FrozenEvaluationTests(unittest.TestCase):
    def fixture(self):
        history = dict(items=[dict(sourceId="procedural-song-00000",
            **fingerprint(token_events(["い:62:480", "う:67:960"])))])
        items = [dict(sourceId="procedural-song-01000", tokens=["あ:60:480"],
                      fingerprints=fingerprint(token_events(["あ:60:480"])))]
        plan = dict(formatId="com.project-seam.frozen-phrase-evaluation-plan", schemaVersion=1,
            purpose="same-voice-unseen-phrase-engineering-evaluation", ppq=960, tempoBpm=120,
            candidateRendered=False, releaseEligible=False, historicalSourceCount=1, items=items)
        return plan, history

    def test_valid_plan_and_changed_tokens(self):
        plan, history = self.fixture()
        self.assertEqual(validate_plan(plan, history), plan["items"])
        plan["items"][0]["tokens"] = ["か:60:480"]
        with self.assertRaisesRegex(ValueError, "fingerprints"):
            validate_plan(plan, history)

    def test_duplicate_ids_and_historical_or_internal_reuse(self):
        for mode in ("identity", "historical", "internal"):
            plan, history = self.fixture()
            if mode == "identity":
                plan["items"][0]["sourceId"] = history["items"][0]["sourceId"]
            elif mode == "historical":
                history["items"][0].update(plan["items"][0]["fingerprints"])
            else:
                other = copy.deepcopy(plan["items"][0])
                other["sourceId"] = "procedural-song-01001"
                plan["items"].append(other)
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                validate_plan(plan, history)

    def test_failed_render_retained_and_output_not_overwritten(self):
        plan, history = self.fixture()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            historical = root / "history.json"
            historical.write_text(json.dumps(history))
            plan["historicalIndexSha256"] = hashlib.sha256(historical.read_bytes()).hexdigest()
            frozen = root / "plan.json"
            frozen.write_text(json.dumps(plan))
            pilot = root / "pilot"
            pilot.write_text("fixture")
            pilot.chmod(0o700)
            args = dict(plan=frozen, plan_sha256=hashlib.sha256(frozen.read_bytes()).hexdigest(),
                        history=historical, pilot=pilot, output=root / "result")
            with patch("tools.voice_model_training.render_frozen_evaluation.subprocess.run",
                       return_value=subprocess.CompletedProcess([], 1, "", "render failed")):
                report = render(**args)
            self.assertEqual(report["state"], "INCOMPLETE")
            self.assertEqual(len(report["items"]), 1)
            self.assertEqual(report["items"][0]["state"], "FAILED")
            self.assertFalse(report["trainingAdmitted"])
            self.assertTrue((root / "result" / "capture.json").is_file())
            with self.assertRaisesRegex(ValueError, "must be new"):
                render(**args)


if __name__ == "__main__":
    unittest.main()
