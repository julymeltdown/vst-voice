import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13a'))

import release_gate  # noqa: E402


class ReleaseGateTests(unittest.TestCase):
    def matrix(self):
        return {
            'schemaVersion': 1,
            'policy': 'MANDATORY',
            'targets': [
                {
                    'id': 'linux-vst3-validator',
                    'category': 'validator',
                    'implementationState': 'CI_CONFIGURED',
                    'runtimeResult': 'NOT_RUN',
                    'mandatoryFor': ['G4'],
                    'evidence': [],
                },
                {
                    'id': 'macos-auval',
                    'category': 'validator',
                    'implementationState': 'CI_CONFIGURED',
                    'runtimeResult': 'NOT_RUN',
                    'mandatoryFor': ['G4'],
                    'evidence': [],
                },
                {
                    'id': 'reaper-windows',
                    'category': 'daw',
                    'implementationState': 'SOURCE_READY',
                    'runtimeResult': 'NOT_RUN',
                    'mandatoryFor': ['G3', 'G4'],
                    'evidence': [],
                },
            ],
        }

    def test_source_ready_is_not_runtime_pass(self):
        result = release_gate.evaluate_matrix(self.matrix(), 'G4')
        self.assertFalse(result.passed)
        self.assertIn('linux-vst3-validator', result.blocked_ids)
        self.assertIn('macos-auval', result.blocked_ids)
        self.assertIn('reaper-windows', result.blocked_ids)

    def test_pass_requires_complete_evidence(self):
        matrix = self.matrix()
        for target in matrix['targets']:
            target['runtimeResult'] = 'PASS'
        result = release_gate.evaluate_matrix(matrix, 'G4')
        self.assertFalse(result.passed)
        self.assertTrue(any('evidence' in item for item in result.errors))

    def test_g4_passes_only_with_real_target_evidence(self):
        matrix = self.matrix()
        evidence = {
            'osVersion': 'actual-os-version',
            'hostVersion': 'actual-host-or-validator-version',
            'pluginSha256': 'a' * 64,
            'executedAt': '2026-08-18T10:00:00Z',
            'executor': 'release-engineer',
            'logs': ['evidence/log.txt'],
        }
        for target in matrix['targets']:
            target['runtimeResult'] = 'PASS'
            target['evidence'] = [evidence]
        result = release_gate.evaluate_matrix(matrix, 'G4')
        self.assertTrue(result.passed, result.errors)

    def test_cli_returns_blocked_exit_code(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'matrix.json'
            path.write_text(json.dumps(self.matrix()), encoding='utf-8')
            self.assertEqual(3, release_gate.main(['check', '--matrix', str(path), '--gate', 'G4']))


if __name__ == '__main__':
    unittest.main()
