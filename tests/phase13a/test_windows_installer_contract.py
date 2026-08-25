from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class WindowsInstallerContractTests(unittest.TestCase):
    def test_installer_ownership_is_explicit_and_preserves_user_roots(self) -> None:
        ownership = json.loads(
            (ROOT / "packaging/windows/installer-ownership.json").read_text(encoding="utf-8")
        )
        nsi = (ROOT / "packaging/windows/ProjectSEAM.nsi").read_text(encoding="utf-8")
        builder = (ROOT / "scripts/build_windows_installer.ps1").read_text(encoding="utf-8")
        self.assertEqual("windows-x64", ownership["platform"])
        self.assertTrue(ownership["ownedPaths"])
        self.assertTrue(ownership["preservedUserRoots"])
        self.assertEqual("separate-explicit-action", ownership["destructiveDataRemoval"])
        self.assertIn("$PROGRAMFILES64\\ProjectSEAM", nsi)
        self.assertIn("$COMMONFILES64\\CLAP", nsi)
        self.assertIn("$COMMONFILES64\\VST3", nsi)
        self.assertIn("Documentation\\external-beta-documentation.json", builder)
        self.assertNotIn("$LOCALAPPDATA", nsi)


if __name__ == "__main__":
    unittest.main()
