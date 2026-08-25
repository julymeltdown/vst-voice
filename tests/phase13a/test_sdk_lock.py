import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13a'))

import sdk_lock  # noqa: E402


class SdkLockTests(unittest.TestCase):
    def valid_lock(self):
        return {
            'schemaVersion': 1,
            'dependencies': [
                {
                    'name': 'clap',
                    'repository': 'https://github.com/free-audio/clap.git',
                    'tag': '1.2.10',
                    'commit': '195b42a004144fab0b3cf95e9c067187d15365b7',
                    'license': 'MIT',
                },
                {
                    'name': 'clap-wrapper',
                    'repository': 'https://github.com/free-audio/clap-wrapper.git',
                    'tag': 'v0.15.1',
                    'commit': '35f524b771ec09f54c164720bb90f271273b37d3',
                    'license': 'MIT',
                },
                {
                    'name': 'vst3sdk',
                    'repository': 'https://github.com/steinbergmedia/vst3sdk.git',
                    'tag': 'VST3_SDK_3.8.1',
                    'commit': '3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96',
                    'license': 'MIT',
                    'recursiveSubmodules': True,
                },
                {
                    'name': 'AudioUnitSDK',
                    'repository': 'https://github.com/apple/AudioUnitSDK.git',
                    'tag': 'AudioUnitSDK-1.4.0',
                    'commit': 'bd98b31feff57a15989fcfab4cd86dc63382b1ac',
                    'license': 'Apache-2.0',
                },
            ],
        }

    def test_accepts_exact_pinned_permissive_dependencies(self):
        errors = sdk_lock.validate_lock(self.valid_lock())
        self.assertEqual([], errors)

    def test_rejects_floating_ref_and_non_permissive_license(self):
        lock = self.valid_lock()
        lock['dependencies'][0]['commit'] = 'master'
        lock['dependencies'][1]['license'] = 'GPL-3.0'
        errors = sdk_lock.validate_lock(lock)
        self.assertTrue(any('40-character' in error for error in errors))
        self.assertTrue(any('not allowed' in error for error in errors))

    def test_checkout_validation_requires_exact_head_and_license(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkout = root / 'clap-wrapper'
            checkout.mkdir()
            (checkout / '.phase13a-revision').write_text(
                '35f524b771ec09f54c164720bb90f271273b37d3\n', encoding='utf-8')
            (checkout / 'LICENSE').write_text('MIT License\n', encoding='utf-8')
            dep = self.valid_lock()['dependencies'][1]
            self.assertEqual([], sdk_lock.validate_checkout(dep, checkout))
            (checkout / '.phase13a-revision').write_text('0' * 40, encoding='utf-8')
            self.assertTrue(sdk_lock.validate_checkout(dep, checkout))

    def test_optional_source_digest_requires_attested_checkout_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkout = root / 'clap-wrapper'
            checkout.mkdir()
            (checkout / '.phase13a-revision').write_text('35f524b771ec09f54c164720bb90f271273b37d3\n', encoding='utf-8')
            (checkout / '.phase13a-source-sha256').write_text('a' * 64, encoding='utf-8')
            (checkout / 'LICENSE').write_text('MIT License\n', encoding='utf-8')
            dependency = dict(self.valid_lock()['dependencies'][1], sourceSha256='a' * 64)
            self.assertEqual([], sdk_lock.validate_checkout(dependency, checkout))
            (checkout / '.phase13a-source-sha256').write_text('b' * 64, encoding='utf-8')
            self.assertTrue(any('source digest' in error for error in sdk_lock.validate_checkout(dependency, checkout)))


if __name__ == '__main__':
    unittest.main()
