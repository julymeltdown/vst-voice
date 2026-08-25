from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MacosInstallerContractTests(unittest.TestCase):
    def test_installer_ownership_is_explicit_and_outer_package_is_signed(self) -> None:
        ownership = json.loads(
            (ROOT / "packaging/macos/installer-ownership.json").read_text(encoding="utf-8")
        )
        package = (ROOT / "scripts/package_macos_plugins.sh").read_text(encoding="utf-8")
        standalone = (ROOT / "scripts/package_macos_standalone.sh").read_text(encoding="utf-8")
        distribution = (ROOT / "packaging/macos/Distribution.xml.in").read_text(encoding="utf-8")
        self.assertEqual("macos-arm64", ownership["platform"])
        self.assertTrue(ownership["ownedPaths"])
        self.assertTrue(ownership["preservedUserRoots"])
        self.assertEqual("separate-explicit-action", ownership["destructiveDataRemoval"])
        self.assertIn("productbuild --sign", package)
        self.assertIn("Library/Application Support/ProjectSEAM/Documentation", package)
        self.assertIn("Manual/EULA.md", standalone)
        self.assertIn("@PROJECT_SEAM_VERSION@", distribution)
        uninstall = (ROOT / "scripts/uninstall_macos_plugins.sh").read_text(encoding="utf-8")
        self.assertNotIn('"/Library/Application Support/ProjectSEAM"\n', uninstall)
        self.assertIn("rmdir", uninstall)


if __name__ == "__main__":
    unittest.main()
