import hashlib
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13b"))

import character_release  # noqa: E402


class CharacterReleaseTests(unittest.TestCase):
    def _evidence(self, root: Path, name: str):
        path = root / "evidence" / f"{name}.bin"
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(name.encode("utf-8"))
        return {
            "path": f"evidence/{name}.bin",
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "kind": name,
            "executedAt": "2026-08-18T12:00:00Z",
            "reviewer": "phase13b-test",
        }

    def _accepted(self, root: Path):
        gates = {
            gate: {"status": "PASS", "evidence": [self._evidence(root, gate)]}
            for gate in character_release.REQUIRED_GATES
        }
        return {
            "schemaVersion": 1,
            "characterId": "official.character.01",
            "version": "1.0.0",
            "publicName": "Test Cleared Name",
            "developmentOnly": False,
            "gates": gates,
        }

    def test_complete_evidence_backed_character_dossier_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = character_release.evaluate_character_dossier(self._accepted(root), root)
            self.assertEqual("ACCEPTED", result["releaseStatus"], result)

    def test_placeholder_character_name_blocks_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = self._accepted(root)
            dossier["publicName"] = "Character 01"
            result = character_release.evaluate_character_dossier(dossier, root)
            self.assertEqual("FAIL", result["releaseStatus"])
            self.assertTrue(any("public name" in error.lower() for error in result["errors"]))

    def test_current_development_assets_do_not_satisfy_production_model_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = self._accepted(root)
            dossier["developmentOnly"] = True
            dossier["gates"]["production-low-poly-model"] = {
                "status": "BLOCKED",
                "evidence": [],
            }
            result = character_release.evaluate_character_dossier(dossier, root)
            self.assertEqual("BLOCKED", result["releaseStatus"])
            self.assertIn("production-low-poly-model", result["unresolved"])


if __name__ == "__main__":
    unittest.main()
