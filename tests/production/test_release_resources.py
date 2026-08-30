from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "packaging/release-resource-inventory.json"


class ReleaseResourceContractTests(unittest.TestCase):
    def test_inventory_keeps_voicebanks_external_and_character_optional(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))

        self.assertEqual(1, inventory["schemaVersion"])
        self.assertEqual([], inventory["bundledVoicebanks"])
        self.assertEqual("per-user-installed-catalog", inventory["voicebankHandoff"])
        self.assertFalse(inventory["characterPackage"]["required"])
        self.assertFalse(inventory["characterPackage"]["bundled"])

    def test_release_packagers_do_not_copy_engineering_assets(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        scripts = "\n".join(
            (ROOT / path).read_text(encoding="utf-8")
            for path in (
                "scripts/package_macos_standalone.sh",
                "scripts/build_windows_installer.ps1",
                "scripts/package_macos_clap.sh",
                "scripts/package_windows_plugin.ps1",
            )
        )

        self.assertNotIn("SEAM_PRODUCTION_DEMO_BANK_SOURCE", cmake)
        self.assertNotIn("SEAM_SOURCE_CHARACTER_PACKAGE", cmake)
        self.assertNotIn("install(DIRECTORY assets/character-01", cmake)
        self.assertNotIn("assets/demo-human-voicebank-public-domain", scripts)
        self.assertNotIn("assets/character-01", scripts)
        self.assertGreaterEqual(cmake.count("-E remove_directory"), 6)
        self.assertIn("Resources/character-01", cmake)
        self.assertIn("Resources/voicebanks", cmake)

    def test_platform_packagers_require_the_release_inventory(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        macos = (ROOT / "scripts/package_macos_standalone.sh").read_text(
            encoding="utf-8"
        )
        windows = (ROOT / "scripts/build_windows_installer.ps1").read_text(
            encoding="utf-8"
        )
        wrappers = (
            ROOT / "packaging/phase13a/wrapper-project/CMakeLists.txt"
        ).read_text(
            encoding="utf-8"
        )

        self.assertIn("release-resource-inventory.json", macos)
        self.assertIn("release-resource-inventory.json", windows)
        self.assertIn("release-resource-inventory.json", wrappers)
        self.assertIn("seam_phase11_clap_editor_missing_bank_smoke", cmake)
        self.assertIn("--expect-missing-bank", cmake)
        self.assertNotIn("character-01\\manifest.json", windows)


if __name__ == "__main__":
    unittest.main()
