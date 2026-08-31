from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.voicebank_production import (
    initialize_production_workspace,
    validate_production_workspace,
    validate_source_strategy_document,
)
from tools.voicebank_script_generator import generate_inventory


ROOT = Path(__file__).resolve().parents[2]


class VoiceSourceAdmissionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.strategy_path = ROOT / "docs/voicebank/BETA_VOICE_SOURCE_STRATEGIES.json"
        self.strategies = json.loads(self.strategy_path.read_text(encoding="utf-8"))

    def test_selected_acquisition_strategy_is_feasible_without_claiming_real_assets(self) -> None:
        result = validate_source_strategy_document(self.strategies, ROOT)
        self.assertTrue(result.passed, result.errors)
        self.assertEqual("READY_FOR_ACQUISITION", self.strategies["status"])
        self.assertEqual("NOT_RUN", self.strategies["assetAdmissionStatus"])
        selected = next(
            item for item in self.strategies["strategies"]
            if item["id"] == self.strategies["selectedStrategyId"]
        )
        self.assertEqual("HUMAN_RECORDING", selected["kind"])
        self.assertEqual("CONTRACT_TEMPLATE_READY", selected["evidenceState"])

    def test_tts_missing_bank_redistribution_right_cannot_be_selected(self) -> None:
        changed = copy.deepcopy(self.strategies)
        changed["selectedStrategyId"] = "commercial-output-tts-example"
        result = validate_source_strategy_document(changed, ROOT)
        self.assertFalse(result.passed)
        self.assertTrue(any("singingBankRedistribution" in error for error in result.errors))

    def test_workspace_initialization_is_inventory_bound_and_rehashes(self) -> None:
        inventory = generate_inventory(
            {
                "profileId": "u56-test-ja",
                "vowels": ["a"],
                "consonants": ["k"],
                "specialPhones": ["br"],
                "includeKinds": ["sustain", "cv"],
                "pitchLayers": [60, 72],
                "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
                "alternateTakes": 1,
            }
        )
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            project = initialize_production_workspace(
                workspace,
                inventory,
                self.strategies,
                project_id="u56-production-test",
                operator_id="operator-a",
                occurred_at="2026-08-31T12:00:00Z",
            )
            self.assertEqual(1, project["lastDurableGeneration"])
            self.assertEqual(inventory["inventorySha256"], project["inventorySha256"])
            self.assertEqual(
                len(inventory["requiredCoverage"]) * len(inventory["pitchLayers"]),
                len(project["unitAssignments"]),
            )
            result = validate_production_workspace(workspace, inventory, self.strategies)
            self.assertTrue(result.passed, result.errors)
            self.assertEqual([], list((workspace / "assets").rglob("*.wav")))
            self.assertEqual([], list((workspace / "staging").iterdir()))

            generation_path = workspace / "generations/00000000000000000001.json"
            changed = json.loads(generation_path.read_text(encoding="utf-8"))
            changed["inventorySha256"] = "f" * 64
            generation_path.write_text(json.dumps(changed), encoding="utf-8")
            result = validate_production_workspace(workspace, inventory, self.strategies)
            self.assertFalse(result.passed)
            self.assertTrue(any("inventorySha256" in error or "journal" in error for error in result.errors))

    def test_cli_initializes_workspace_without_hand_editing_json(self) -> None:
        inventory = generate_inventory(
            {
                "profileId": "u56-cli-ja",
                "vowels": ["a"],
                "consonants": ["k"],
                "specialPhones": ["br"],
                "includeKinds": ["sustain"],
                "pitchLayers": [60, 72],
                "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
                "alternateTakes": 1,
            }
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory_path = root / "inventory.json"
            inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
            workspace = root / "workspace"
            result = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "tools.external_beta.voicebank_production",
                    "init-project",
                    "--inventory",
                    str(inventory_path),
                    "--strategies",
                    str(self.strategy_path),
                    "--workspace",
                    str(workspace),
                    "--project-id",
                    "u56-cli-project",
                    "--operator-id",
                    "operator-a",
                    "--occurred-at",
                    "2026-08-31T12:30:00Z",
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertTrue((workspace / "project.json").is_file())

    def test_workspace_inputs_fail_closed_on_invalid_time_and_record_shapes(self) -> None:
        inventory = generate_inventory(
            {
                "profileId": "u56-malformed-ja",
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
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "UTC timestamp"):
                initialize_production_workspace(
                    root / "invalid-time",
                    inventory,
                    self.strategies,
                    project_id="invalid-time",
                    operator_id="operator-a",
                    occurred_at="not-a-time",
                )

            workspace = root / "malformed"
            initialize_production_workspace(
                workspace,
                inventory,
                self.strategies,
                project_id="malformed",
                operator_id="operator-a",
                occurred_at="2026-08-31T12:45:00Z",
            )
            generation_path = workspace / "generations/00000000000000000001.json"
            generation = json.loads(generation_path.read_text(encoding="utf-8"))
            generation["assets"] = None
            payload = (
                json.dumps(
                    generation,
                    ensure_ascii=False,
                    sort_keys=True,
                    indent=2,
                    allow_nan=False,
                )
                + "\n"
            ).encode("utf-8")
            generation_path.write_bytes(payload)
            journal_path = workspace / "journal/00000000000000000001.json"
            journal = json.loads(journal_path.read_text(encoding="utf-8"))
            journal["projectSha256"] = hashlib.sha256(payload).hexdigest()
            journal_path.write_text(
                json.dumps(journal, sort_keys=True, indent=2) + "\n",
                encoding="utf-8",
            )
            result = validate_production_workspace(
                workspace, inventory, self.strategies
            )
            self.assertFalse(result.passed)
            self.assertTrue(any("assets must be an array" in error for error in result.errors))


if __name__ == "__main__":
    unittest.main()
