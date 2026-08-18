import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13a'))

import distribution_manifest  # noqa: E402


class DistributionManifestTests(unittest.TestCase):
    def test_declared_vst3_requires_real_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / 'ProjectSEAMEditor.vst3' / 'Contents' / 'x86_64-linux'
            bundle.mkdir(parents=True)
            errors = distribution_manifest.validate_artifact('vst3', bundle.parents[1])
            self.assertTrue(any('binary' in error.lower() for error in errors))
            (bundle / 'ProjectSEAMEditor.so').write_bytes(b'\x7fELF' + b'\0' * 64)
            self.assertEqual([], distribution_manifest.validate_artifact('vst3', bundle.parents[1]))

    def test_auv2_must_be_validated_only_on_macos(self):
        with tempfile.TemporaryDirectory() as directory:
            component = Path(directory) / 'ProjectSEAMEditor.component'
            (component / 'Contents' / 'MacOS').mkdir(parents=True)
            (component / 'Contents' / 'MacOS' / 'ProjectSEAMEditor').write_bytes(b'MachO')
            errors = distribution_manifest.validate_artifact('auv2', component, platform='linux')
            self.assertTrue(any('macOS' in error for error in errors))

    def test_release_manifest_never_promotes_not_run_validation(self):
        manifest = distribution_manifest.build_release_manifest(
            version='0.13.0',
            artifacts=[],
            validations={'vst3-validator': 'NOT_RUN', 'auval': 'NOT_RUN'},
        )
        self.assertEqual('BLOCKED', manifest['releaseStatus'])
        self.assertEqual(2, manifest['unresolvedMandatoryCount'])

    def test_bundle_digest_changes_when_nested_binary_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'ProjectSEAMEditor.vst3'
            binary = root / 'Contents' / 'x86_64-linux' / 'ProjectSEAMEditor.so'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'one')
            first = distribution_manifest.sha256_path(root)
            binary.write_bytes(b'two')
            second = distribution_manifest.sha256_path(root)
            self.assertNotEqual(first, second)
            self.assertEqual(64, len(first))

    def test_windows_single_file_vst3_is_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            plugin = Path(directory) / 'ProjectSEAMEditor.vst3'
            plugin.write_bytes(b'MZ' + b'\0' * 128)
            self.assertEqual([], distribution_manifest.validate_artifact('vst3', plugin, platform='windows'))


if __name__ == '__main__':
    unittest.main()
