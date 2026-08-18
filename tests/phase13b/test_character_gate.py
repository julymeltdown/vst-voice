import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13b'))

from character_audit import audit_character  # noqa: E402
from character_gate import REQUIRED_CATEGORIES, evaluate_character_dossier  # noqa: E402


def evidence(root: Path, name: str):
    path = root / 'evidence' / f'{name}.txt'
    path.parent.mkdir(exist_ok=True)
    path.write_text(f'{name} approved\n', encoding='utf-8')
    return {
        'type': name,
        'path': str(path.relative_to(root)),
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'executedAt': '2026-08-18T00:00:00Z',
        'reviewer': 'character-reviewer',
        'result': 'PASS',
    }


def make_character(root: Path):
    character = root / 'character'
    required = [
        'turnaround/front.png', 'turnaround/side.png', 'turnaround/back.png',
        'model/production.obj', 'model/production.mtl', 'model/uv.png',
        'lod/lod0.obj', 'lod/lod1.obj', 'animation/idle.anim',
        'key-art/key-art.png', 'runtime/neutral.ppm', 'runtime/focused.ppm',
        'runtime/rendering.ppm', 'runtime/complete.ppm', 'runtime/warning.ppm', 'runtime/error.ppm',
    ]
    for rel in required:
        path = character / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(f'asset:{rel}'.encode())
    manifest = {
        'schemaVersion': 2,
        'characterId': 'official.character.01',
        'displayName': 'Seam Character',
        'version': '1.0.0',
        'voicebankId': 'official.voice.01',
        'states': {name: f'runtime/{name}.ppm' for name in ['neutral', 'focused', 'rendering', 'complete', 'warning', 'error']},
    }
    (character / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
    return character, required


class CharacterGateTests(unittest.TestCase):
    def valid_dossier(self, root: Path, character: Path, required):
        return {
            'schemaVersion': 1,
            'component': 'official-character',
            'characterId': 'official.character.01',
            'version': '1.0.0',
            'commercialRelease': True,
            'finalPublicName': 'Seam Character',
            'characterRoot': str(character.relative_to(root)),
            'assetProfile': {
                'requiredPaths': required,
                'requiredStates': ['neutral', 'focused', 'rendering', 'complete', 'warning', 'error'],
                'minimumLods': 2,
            },
            'requirements': {
                category: {'result': 'PASS', 'evidence': [evidence(root, category)]}
                for category in REQUIRED_CATEGORIES
            },
        }

    def test_valid_synthetic_character_dossier_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            character, required = make_character(root)
            result = evaluate_character_dossier(self.valid_dossier(root, character, required), root)
            self.assertTrue(result.passed, result.errors)

    def test_nonfinal_character_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            character, required = make_character(root)
            dossier = self.valid_dossier(root, character, required)
            dossier['commercialRelease'] = False
            dossier['finalPublicName'] = 'Character 01'
            result = evaluate_character_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any('commercialRelease' in error for error in result.errors))
            self.assertTrue(any('finalPublicName' in error for error in result.errors))

    def test_missing_production_asset_blocks_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            character, required = make_character(root)
            (character / 'model' / 'production.obj').unlink()
            result = evaluate_character_dossier(self.valid_dossier(root, character, required), root)
            self.assertFalse(result.passed)
            self.assertTrue(any('production.obj' in error for error in result.errors))

    def test_character_evidence_tampering_blocks_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            character, required = make_character(root)
            dossier = self.valid_dossier(root, character, required)
            first = next(iter(REQUIRED_CATEGORIES))
            (root / dossier['requirements'][first]['evidence'][0]['path']).write_text('tampered', encoding='utf-8')
            result = evaluate_character_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any('sha256' in error for error in result.errors))

    def test_character_root_symlink_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as external_directory:
            root = Path(directory)
            external_root = Path(external_directory)
            external_character, required = make_character(external_root)
            link = root / 'character-link'
            try:
                link.symlink_to(external_character, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f'symlink creation unavailable: {exc}')
            dossier = self.valid_dossier(root, link, required)
            result = evaluate_character_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any('characterRoot' in error or 'symlink' in error or 'escapes' in error for error in result.errors))

    def test_character_audit_counts_lods_and_states(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            character, required = make_character(root)
            report = audit_character(character, {'requiredPaths': required, 'requiredStates': ['neutral'], 'minimumLods': 2})
            self.assertTrue(report['passed'], report['errors'])
            self.assertEqual(2, report['lodCount'])


if __name__ == '__main__':
    unittest.main()
