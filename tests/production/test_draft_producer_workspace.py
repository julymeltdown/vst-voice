"""Cross-language parity for producer workspaces created by Studio and the CLI.

The C++ generator must reproduce tools/voicebank_script_generator/draft_inventory.py
byte for byte, and the producer it initializes must be exactly the draft that
prepare_production_draft_definition() describes, so the external validator admits
the workspace without any hand-written definition.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.voicebank_production import (
    prepare_production_draft_definition,
    validate_production_draft_workspace,
)
from tools.voicebank_script_generator.draft_inventory import (
    generate_draft_inventory,
    render_draft_operator_csv,
    validate_draft_inventory,
)

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "build/release/seam_voicebank_cli"
CUSTOM_PROFILE = {
    "profileId": "custom \u2014 voice",
    "supportedStyles": ["soft, \"airy\"", "\u660e\u308b\u3044"],
    "vowels": ["a", "o"],
    "consonants": ["k", "sh"],
    "specialPhones": ["N", "br"],
    "includeKinds": ["sustain", "breath", "cv", "vv"],
    "pitchLayers": [64, 57],
    "requestedRange": {"minMidi": 55, "maxMidi": 70},
    "alternateTakes": 1,
    "sessionBlockSize": 3,
}
# Studio's vowel starter preset (draftInventoryPresetProfile): five sustained vowels at two pitches.
VOWEL_STARTER_PROFILE = {"profileId": "parity-voice", "includeKinds": ["sustain"], "pitchLayers": [60, 66]}


def _create(destination: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(CLI), "new-producer-workspace", str(destination), "parity-voice", "producer",
         "2026-09-29T00:00:00Z", *extra],
        capture_output=True, text=True, timeout=180, check=False)


@unittest.skipUnless(CLI.is_file(), "Build the actual production CLI for cross-language parity")
class DraftProducerWorkspaceParityTest(unittest.TestCase):
    def assert_parity(self, root: Path, profile: dict, receipt: dict) -> None:
        inventory = json.loads((root / "inventory.json").read_text(encoding="utf-8"))
        self.assertEqual(validate_draft_inventory(inventory), [])
        self.assertEqual(inventory, generate_draft_inventory(profile))
        script = (root / "recording-script.csv").read_bytes()
        self.assertEqual(script.decode("utf-8"), render_draft_operator_csv(inventory))
        self.assertEqual(hashlib.sha256(script).hexdigest(), inventory["scriptSha256"])
        self.assertEqual(receipt["inventorySha256"], inventory["inventorySha256"])
        self.assertEqual(receipt["scriptSha256"], inventory["scriptSha256"])
        self.assertEqual(receipt["units"], len(inventory["units"]))
        self.assertEqual(receipt["rangeAssessment"], "NOT_ASSESSED")
        self.assertFalse(receipt["releaseEligible"])

        workspace = root / "producer"
        project = json.loads((workspace / "project.json").read_text(encoding="utf-8"))
        expected = prepare_production_draft_definition(
            inventory, None, project_id="parity-voice", operator_id="producer", repository_root=ROOT)
        self.assertEqual(project["lastDurableGeneration"], int(receipt["generation"]))
        expected["lastDurableGeneration"] = project["lastDurableGeneration"]
        self.assertEqual(project, expected)
        self.assertEqual(receipt["assignments"], len(project["unitAssignments"]))
        self.assertEqual(hashlib.sha256((workspace / "project.json").read_bytes()).hexdigest(), receipt["projectSha256"])
        verified = validate_production_draft_workspace(workspace, inventory)
        self.assertTrue(verified.passed, verified.errors)

    def test_default_profile_workspace_matches_the_external_definition(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve() / "default-voice"
            created = _create(root)
            self.assertEqual(created.returncode, 0, created.stderr)
            receipt = json.loads(created.stdout)
            self.assertEqual(Path(receipt["root"]), root)
            self.assert_parity(root, {"profileId": "parity-voice"}, receipt)

    def test_captured_profile_with_quoted_and_non_ascii_styles(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            payload = json.dumps(CUSTOM_PROFILE, ensure_ascii=False).encode("utf-8")
            (base / "profile.json").write_bytes(payload)
            root = base / "custom-voice"
            created = _create(root, str(base / "profile.json"), hashlib.sha256(payload).hexdigest())
            self.assertEqual(created.returncode, 0, created.stderr)
            receipt = json.loads(created.stdout)
            self.assertEqual(receipt["units"], 36)
            self.assert_parity(root, CUSTOM_PROFILE, receipt)

    def test_vowel_starter_workspace_matches_the_external_definition(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            payload = json.dumps(VOWEL_STARTER_PROFILE).encode("utf-8")
            (base / "profile.json").write_bytes(payload)
            root = base / "starter-voice"
            created = _create(root, str(base / "profile.json"), hashlib.sha256(payload).hexdigest())
            self.assertEqual(created.returncode, 0, created.stderr)
            receipt = json.loads(created.stdout)
            self.assertEqual(receipt["units"], 20)
            self.assertEqual(receipt["assignments"], 10)
            self.assert_parity(root, VOWEL_STARTER_PROFILE, receipt)

    def test_refusals_leave_nothing_behind(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            payload = json.dumps({"profileId": "x", "rangeTest": {"result": "PASS"}}).encode("utf-8")
            (base / "profile.json").write_bytes(payload)
            refused = _create(base / "range-test", str(base / "profile.json"), hashlib.sha256(payload).hexdigest())
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("not draft authority", refused.stderr)
            changed = _create(base / "changed", str(base / "profile.json"), "0" * 64)
            self.assertNotEqual(changed.returncode, 0)
            self.assertEqual(sorted(path.name for path in base.iterdir()), ["profile.json"])
            first = _create(base / "voice")
            self.assertEqual(first.returncode, 0, first.stderr)
            before = (base / "voice" / "inventory.json").read_bytes()
            again = _create(base / "voice")
            self.assertNotEqual(again.returncode, 0)
            self.assertIn("already exists", again.stderr)
            self.assertEqual((base / "voice" / "inventory.json").read_bytes(), before)
            self.assertEqual(sorted(path.name for path in base.iterdir()), ["profile.json", "voice"])


if __name__ == "__main__":
    unittest.main()
