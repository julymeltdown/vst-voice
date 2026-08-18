import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13a'))

import developer_package  # noqa: E402
import linux_package_smoke  # noqa: E402


class LinuxPackageSmokeTests(unittest.TestCase):
    def test_developer_zip_installs_and_uninstalls_without_touching_real_home(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            clap = root / 'ProjectSEAMEditor.clap'
            clap.write_bytes(b'clap-binary')
            resources = root / 'ProjectSEAMEditor.resources'
            resources.mkdir()
            (resources / 'manifest.json').write_text('{}', encoding='utf-8')
            package = root / 'package.zip'
            developer_package.create_developer_package(clap, resources, package, '0.13.0')
            result = linux_package_smoke.run_smoke(package, root / 'sandbox')
            self.assertEqual('PASS', result['status'])
            self.assertTrue(result['installedClap'])
            self.assertTrue(result['uninstalledClap'])

    def test_path_traversal_is_rejected(self):
        import zipfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / 'bad.zip'
            with zipfile.ZipFile(package, 'w') as archive:
                archive.writestr('../escape', b'x')
            with self.assertRaises(ValueError):
                linux_package_smoke.run_smoke(package, root / 'sandbox')


if __name__ == '__main__':
    unittest.main()
