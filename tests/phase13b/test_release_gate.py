import hashlib
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13b'))

from release_gate import evaluate_g5  # noqa: E402


def evidence(root: Path, name: str):
    path = root / 'evidence' / f'{name}.txt'
    path.parent.mkdir(exist_ok=True)
    path.write_text(name, encoding='utf-8')
    return {
        'kind': name,
        'path': str(path.relative_to(root)),
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'executedAt': '2026-08-18T00:00:00Z',
        'reviewer': 'release-owner',
    }


class ReleaseGateTests(unittest.TestCase):
    def test_g5_is_blocked_when_external_target_is_not_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = evaluate_g5(
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                [{'targets': [{'id': 'windows-runtime', 'mandatoryFor': ['G5'], 'runtimeResult': 'NOT_RUN', 'evidence': []}]}],
                {'requirements': {
                    'final-eula': {'result': 'PASS', 'evidence': [evidence(root, 'eula')]},
                    'voicebank-license': {'result': 'PASS', 'evidence': [evidence(root, 'license')]},
                }},
                root,
            )
            self.assertFalse(result['passed'])
            self.assertIn('windows-runtime', result['blockedTargets'])

    def test_g5_passes_only_with_accepted_components_external_evidence_and_licenses(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = evaluate_g5(
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                [{'targets': [{'id': 'windows-runtime', 'mandatoryFor': ['G5'], 'runtimeResult': 'PASS', 'evidence': [evidence(root, 'runtime')]}]}],
                {'requirements': {
                    'final-eula': {'result': 'PASS', 'evidence': [evidence(root, 'eula')]},
                    'voicebank-license': {'result': 'PASS', 'evidence': [evidence(root, 'license')]},
                }},
                root,
            )
            self.assertTrue(result['passed'], result)
            self.assertEqual(0, result['unresolvedMandatoryCount'])

    def test_g5_rejects_tampered_external_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = evidence(root, 'runtime')
            (root / runtime['path']).write_text('tampered', encoding='utf-8')
            result = evaluate_g5(
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                [{'targets': [{'id': 'windows-runtime', 'mandatoryFor': ['G5'], 'runtimeResult': 'PASS', 'evidence': [runtime]}]}],
                {'requirements': {
                    'final-eula': {'result': 'PASS', 'evidence': [evidence(root, 'eula')]},
                    'voicebank-license': {'result': 'PASS', 'evidence': [evidence(root, 'license')]},
                }},
                root,
            )
            self.assertFalse(result['passed'])
            self.assertTrue(any('sha256' in error for error in result['errors']))

    def test_g5_accepts_verified_phase13a_evidence_record(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / 'evidence' / 'host.log'
            log.parent.mkdir(exist_ok=True)
            log.write_text('host validation pass', encoding='utf-8')
            relative = str(log.relative_to(root))
            record = {
                'osVersion': 'Windows 11 24H2',
                'hostVersion': 'REAPER 7.0',
                'pluginFormat': 'CLAP',
                'pluginSha256': 'a' * 64,
                'executedAt': '2026-08-18T00:00:00Z',
                'executor': 'release-owner',
                'checks': {'scan': 'PASS', 'transport': 'PASS'},
                'logs': [relative],
                'evidenceSha256': {relative: hashlib.sha256(log.read_bytes()).hexdigest()},
            }
            result = evaluate_g5(
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                [{'targets': [{'id': 'reaper', 'mandatoryFor': ['G5'], 'runtimeResult': 'PASS', 'evidence': [record]}]}],
                {'requirements': {
                    'final-eula': {'result': 'PASS', 'evidence': [evidence(root, 'eula')]},
                    'voicebank-license': {'result': 'PASS', 'evidence': [evidence(root, 'license')]},
                }},
                root,
            )
            self.assertTrue(result['passed'], result)

    def test_g5_rejects_tampered_phase13a_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / 'evidence' / 'host.log'
            log.parent.mkdir(exist_ok=True)
            log.write_text('host validation pass', encoding='utf-8')
            relative = str(log.relative_to(root))
            digest = hashlib.sha256(log.read_bytes()).hexdigest()
            log.write_text('tampered', encoding='utf-8')
            record = {
                'osVersion': 'macOS 26',
                'hostVersion': 'Logic Pro',
                'pluginFormat': 'AUv2',
                'pluginSha256': 'b' * 64,
                'executedAt': '2026-08-18T00:00:00Z',
                'executor': 'release-owner',
                'checks': {'scan': 'PASS', 'transport': 'PASS'},
                'logs': [relative],
                'evidenceSha256': {relative: digest},
            }
            result = evaluate_g5(
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                {'releaseStatus': 'ACCEPTED', 'errors': [], 'unresolved': []},
                [{'targets': [{'id': 'logic-pro', 'mandatoryFor': ['G5'], 'runtimeResult': 'PASS', 'evidence': [record]}]}],
                {'requirements': {
                    'final-eula': {'result': 'PASS', 'evidence': [evidence(root, 'eula')]},
                    'voicebank-license': {'result': 'PASS', 'evidence': [evidence(root, 'license')]},
                }},
                root,
            )
            self.assertFalse(result['passed'])
            self.assertIn('logic-pro', result['blockedTargets'])
            self.assertTrue(any('sha256' in error for error in result['errors']))


if __name__ == '__main__':
    unittest.main()
