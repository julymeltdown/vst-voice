import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13b'))

from voicebank_audit import audit_voicebank  # noqa: E402
from voicebank_gate import REQUIRED_CATEGORIES, evaluate_voicebank_dossier  # noqa: E402


def evidence(root: Path, name: str):
    path = root / 'evidence' / f'{name}.txt'
    path.parent.mkdir(exist_ok=True)
    path.write_text(f'{name} approved\n', encoding='utf-8')
    return {
        'type': name,
        'path': str(path.relative_to(root)),
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'executedAt': '2026-08-18T00:00:00Z',
        'reviewer': 'voicebank-reviewer',
        'result': 'PASS',
    }


def make_bank(root: Path):
    bank = root / 'bank'
    (bank / 'audio').mkdir(parents=True)
    (bank / 'audio' / 'a.wav').write_bytes(b'RIFFdemo-human-audio')
    manifest = {
        'formatId': 'com.project-seam.voicebank',
        'schemaVersion': 3,
        'id': 'official.voice.01',
        'version': '1.0.0',
        'displayName': 'Official Voicebank 01',
        'language': 'ja',
        'styles': ['original'],
        'units': [
            {'id': 'a-cv', 'kind': 'cv', 'phones': ['k', 'a'], 'audio': 'audio/a.wav', 'rootMidi': 60, 'style': 'original', 'enabled': True},
            {'id': 'a-sustain', 'kind': 'sustain', 'phones': ['a'], 'audio': 'audio/a.wav', 'rootMidi': 64, 'style': 'original', 'enabled': True},
            {'id': 'a-release', 'kind': 'release', 'phones': ['a', 'R'], 'audio': 'audio/a.wav', 'rootMidi': 67, 'style': 'original', 'enabled': True},
        ],
    }
    (bank / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
    return bank


class VoicebankGateTests(unittest.TestCase):
    def valid_dossier(self, root: Path, bank: Path):
        return {
            'schemaVersion': 1,
            'component': 'official-voicebank',
            'voicebankId': 'official.voice.01',
            'version': '1.0.0',
            'official': True,
            'contractedSinger': True,
            'bankRoot': str(bank.relative_to(root)),
            'inventoryProfile': {
                'minimumEnabledUnits': 3,
                'minimumPitchLayers': 3,
                'requiredKinds': {'cv': 1, 'sustain': 1, 'release': 1},
                'requiredStyles': ['original'],
                'requiredPhones': ['a', 'k', 'R'],
            },
            'requirements': {
                category: {'result': 'PASS', 'evidence': [evidence(root, category)]}
                for category in REQUIRED_CATEGORIES
            },
        }

    def test_valid_synthetic_official_dossier_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bank = make_bank(root)
            result = evaluate_voicebank_dossier(self.valid_dossier(root, bank), root)
            self.assertTrue(result.passed, result.errors)

    def test_demo_or_uncontracted_bank_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bank = make_bank(root)
            dossier = self.valid_dossier(root, bank)
            dossier['official'] = False
            dossier['contractedSinger'] = False
            result = evaluate_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any('official' in error for error in result.errors))
            self.assertTrue(any('contractedSinger' in error for error in result.errors))

    def test_inventory_deficit_blocks_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bank = make_bank(root)
            dossier = self.valid_dossier(root, bank)
            dossier['inventoryProfile']['minimumEnabledUnits'] = 999
            result = evaluate_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any('minimumEnabledUnits' in error for error in result.errors))

    def test_evidence_tampering_blocks_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bank = make_bank(root)
            dossier = self.valid_dossier(root, bank)
            first = next(iter(REQUIRED_CATEGORIES))
            (root / dossier['requirements'][first]['evidence'][0]['path']).write_text('tampered', encoding='utf-8')
            result = evaluate_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any('sha256' in error for error in result.errors))

    def test_bank_root_symlink_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as external_directory:
            root = Path(directory)
            external_root = Path(external_directory)
            external_bank = make_bank(external_root)
            link = root / 'bank-link'
            try:
                link.symlink_to(external_bank, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f'symlink creation unavailable: {exc}')
            dossier = self.valid_dossier(root, link)
            result = evaluate_voicebank_dossier(dossier, root)
            self.assertFalse(result.passed)
            self.assertTrue(any('bankRoot' in error or 'symlink' in error or 'escapes' in error for error in result.errors))

    def test_audit_reports_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bank = make_bank(root)
            report = audit_voicebank(bank, {'minimumEnabledUnits': 3, 'requiredKinds': {'cv': 1}})
            self.assertTrue(report['passed'], report['errors'])
            self.assertEqual(3, report['enabledUnits'])
            self.assertEqual(3, report['pitchLayers'])


if __name__ == '__main__':
    unittest.main()
