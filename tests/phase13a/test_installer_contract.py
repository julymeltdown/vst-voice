from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class InstallerContractTests(unittest.TestCase):
    def test_windows_installer_is_identity_bound_and_preserves_user_data(self) -> None:
        nsi = (ROOT / "packaging/windows/ProjectSEAM.nsi").read_text(encoding="utf-8")
        builder = (ROOT / "scripts/build_windows_installer.ps1").read_text(encoding="utf-8")
        for token in ("PRODUCT_VERSION", "BUILD_ID", "SOURCE_COMMIT", "$PROGRAMFILES64\\ProjectSEAM"):
            self.assertIn(token, nsi + builder)
        self.assertIn("Standalone\\seam_editor_native.exe", builder)
        self.assertIn("moduleinfo.json", builder)
        self.assertIn("ConvertFrom-Json", builder)
        self.assertIn("RELEASE_IDENTITY.json", builder)
        self.assertIn("release-resource-inventory.json", builder)
        self.assertNotIn("character-01", builder)
        self.assertNotIn("demo-human-voicebank-public-domain", builder)
        self.assertNotIn("$LOCALAPPDATA", nsi)
        self.assertNotIn("0.13.0", nsi + builder)

    def test_macos_package_requires_version_and_uses_signed_outer_package(self) -> None:
        script = (ROOT / "scripts/package_macos_plugins.sh").read_text(encoding="utf-8")
        distribution = (ROOT / "packaging/macos/Distribution.xml.in").read_text(encoding="utf-8")
        self.assertIn("PROJECT_SEAM_VERSION", script)
        self.assertIn("pkgbuild", script)
        self.assertIn("productbuild --sign", script)
        self.assertIn("--norsrc", script)
        self.assertIn("@PROJECT_SEAM_VERSION@", distribution)
        self.assertIn("RELEASE_IDENTITY.json", script)
        standalone = (ROOT / "scripts/package_macos_standalone.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("release-resource-inventory.json", standalone)
        self.assertNotIn("character-01", standalone)
        self.assertNotIn("demo-human-voicebank-public-domain", standalone)
        self.assertNotIn("PROJECT_SEAM_VERSION is required", script)
        self.assertNotIn("0.13.0", script + distribution)


if __name__ == "__main__":
    unittest.main()
