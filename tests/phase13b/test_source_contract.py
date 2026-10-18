import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Phase13BSourceContractTests(unittest.TestCase):
    def test_blocked_dossiers_exist_and_stay_blocked(self):
        required = [
            'docs/phase13b/official-voicebank-01-dossier.json',
            'docs/phase13b/character-01-dossier.json',
            'docs/phase13b/mandatory-validation-matrix.json',
            'scripts/verify_phase13b_contracts.py',
            'phase13b/CMakeLists.txt',
        ]
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)
        voice = json.loads((ROOT / 'docs/phase13b/official-voicebank-01-dossier.json').read_text(encoding='utf-8'))
        character = json.loads((ROOT / 'docs/phase13b/character-01-dossier.json').read_text(encoding='utf-8'))
        self.assertFalse(voice['official'])
        self.assertFalse(voice['contractedSinger'])
        self.assertFalse(character['commercialRelease'])
        self.assertEqual('', character['finalPublicName'])

    def test_checked_in_evidence_uses_portable_artifact_paths(self):
        for name in (
            "development-content-bundle.json",
            "blocked-release-candidate.json",
            "phase13b-summary.json",
        ):
            payload = json.loads((ROOT / "docs/phase13b/evidence" / name).read_text(encoding="utf-8"))
            paths = []
            if isinstance(payload.get("path"), str):
                paths.append(payload["path"])
            bundle = payload.get("developmentBundle")
            if isinstance(bundle, dict) and isinstance(bundle.get("path"), str):
                paths.append(bundle["path"])
            for value in paths:
                self.assertFalse(Path(value).is_absolute(), f"{name}: {value}")
                self.assertNotIn("..", Path(value).parts)


if __name__ == '__main__':
    unittest.main()
