from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MacosInstallerContractTests(unittest.TestCase):
    def test_installer_ownership_is_explicit_and_outer_package_is_signed(self) -> None:
        ownership = json.loads(
            (ROOT / "packaging/macos/installer-ownership.json").read_text(
                encoding="utf-8"
            )
        )
        package = (ROOT / "scripts/package_macos_plugins.sh").read_text(
            encoding="utf-8"
        )
        standalone = (ROOT / "scripts/package_macos_standalone.sh").read_text(
            encoding="utf-8"
        )
        distribution = (ROOT / "packaging/macos/Distribution.xml.in").read_text(
            encoding="utf-8"
        )
        preinstall = (ROOT / "packaging/macos/scripts/preinstall").read_text(
            encoding="utf-8"
        )
        self.assertEqual("macos-arm64", ownership["platform"])
        self.assertEqual(
            {
                "Applications/Project SEAM.app",
                "Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap",
                "Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3",
                "Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component",
                "Library/Application Support/ProjectSEAM/Documentation",
                "Library/Application Support/ProjectSEAM/Trust",
                "Library/Application Support/ProjectSEAM/Ownership",
                "Library/Application Support/ProjectSEAM/Notices",
                "Library/Application Support/ProjectSEAM/Tools/seam_installer_verifier",
                "Library/Application Support/ProjectSEAM/RELEASE_IDENTITY.json",
                "Library/Application Support/ProjectSEAM/release-payload-manifest.json",
                "Library/Application Support/ProjectSEAM/release-dependency-closure.json",
                "Library/Application Support/ProjectSEAM/THIRD_PARTY_NOTICES.md",
                "Library/Application Support/ProjectSEAM/SBOM.spdx.json",
                "Library/Application Support/ProjectSEAM/uninstall_macos_plugins.sh",
            },
            set(ownership["ownedPaths"]),
        )
        self.assertTrue(ownership["preservedUserRoots"])
        self.assertEqual(
            ["Library/Application Support/ProjectSEAM/InstallerReplay"],
            ownership["preservedSystemRoots"],
        )
        self.assertEqual(
            {"com.project-seam.plugins", "com.project-seam.standalone"},
            set(ownership["ownedPackageReceipts"]),
        )
        self.assertEqual(
            "separate-explicit-action", ownership["destructiveDataRemoval"]
        )
        self.assertIn("productbuild --sign", package)
        self.assertIn("Library/Application Support/ProjectSEAM/Documentation", package)
        self.assertIn("Manual/EULA.md", standalone)
        self.assertIn("@PROJECT_SEAM_VERSION@", distribution)
        self.assertIn("com.project-seam.standalone", distribution)
        self.assertIn("release-payload-manifest.json", package)
        self.assertIn("Standalone/Project SEAM.app", package)
        self.assertIn("Tools/seam_installer_verifier", package)
        self.assertIn("Notices/openssl-LICENSE.txt", package)
        self.assertIn("--scripts", package)
        for option in (
            "--handoff",
            "--manifest",
            "--policy",
            "--staging-root",
            "--expected-candidate",
            "--expected-handoff-sha256",
        ):
            self.assertIn(option, preinstall)
        for forbidden in ("--root-key", "--replay-root", "--now"):
            self.assertNotIn(forbidden, preinstall)
        self.assertIn(
            "InstallerReplay",
            (ROOT / "apps/seam-installer-verifier/main.cpp").read_text(
                encoding="utf-8"
            ),
        )
        self.assertLess(
            preinstall.index("seam_installer_verifier"), preinstall.index("PASS")
        )
        uninstall = (ROOT / "scripts/uninstall_macos_plugins.sh").read_text(
            encoding="utf-8"
        )
        self.assertNotIn('"/Library/Application Support/ProjectSEAM"\n', uninstall)
        self.assertIn("rmdir", uninstall)
        oracle = (ROOT / "scripts/test_macos_installer.sh").read_text(encoding="utf-8")
        self.assertIn("ownedPayloadRemoved", oracle)
        self.assertIn("replayStatePreserved", oracle)
        self.assertIn("ownership_values ownedPaths", oracle)
        self.assertIn("ownership_values ownedPackageReceipts", oracle)
        self.assertIn("standaloneLaunch", oracle)
        self.assertIn("open -na", oracle)


if __name__ == "__main__":
    unittest.main()
