import hashlib
import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13a'))

import developer_package  # noqa: E402


class DeveloperPackageTests(unittest.TestCase):
    def test_package_is_deterministic_and_labels_unsigned_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            clap = root / 'ProjectSEAMEditor.clap'
            clap.write_bytes(b'clap-binary')
            resources = root / 'ProjectSEAMEditor.resources'
            resources.mkdir()
            (resources / 'manifest.json').write_text('{}', encoding='utf-8')
            first = root / 'first.zip'
            second = root / 'second.zip'
            developer_package.create_developer_package(clap, resources, first, '0.13.0')
            developer_package.create_developer_package(clap, resources, second, '0.13.0')
            self.assertEqual(hashlib.sha256(first.read_bytes()).hexdigest(), hashlib.sha256(second.read_bytes()).hexdigest())
            with zipfile.ZipFile(first) as archive:
                names = set(archive.namelist())
                self.assertIn('ProjectSEAM/UNSIGNED-DEVELOPMENT-BUILD.txt', names)
                self.assertIn('ProjectSEAM/CLAP/ProjectSEAMEditor.clap', names)
                self.assertIn('ProjectSEAM/THIRD_PARTY_NOTICES.md', names)
                self.assertIn('ProjectSEAM/SBOM.spdx.json', names)
                self.assertIn('ProjectSEAM/install.sh', names)
                self.assertIn('ProjectSEAM/uninstall.sh', names)

    def test_empty_clap_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            clap = root / 'ProjectSEAMEditor.clap'
            clap.write_bytes(b'')
            resources = root / 'resources'
            resources.mkdir()
            with self.assertRaises(ValueError):
                developer_package.create_developer_package(clap, resources, root / 'x.zip', '0.13.0')


if __name__ == '__main__':
    unittest.main()
