from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class WindowsInstallerContractTests(unittest.TestCase):
    def test_installer_ownership_is_explicit_and_preserves_user_roots(self) -> None:
        ownership = json.loads(
            (ROOT / "packaging/windows/installer-ownership.json").read_text(
                encoding="utf-8"
            )
        )
        nsi = (ROOT / "packaging/windows/ProjectSEAM.nsi").read_text(encoding="utf-8")
        builder = (ROOT / "scripts/build_windows_installer.ps1").read_text(
            encoding="utf-8"
        )
        signing = (ROOT / "scripts/sign_windows_payload.ps1").read_text(
            encoding="utf-8"
        )
        oracle = (ROOT / "scripts/test_windows_installer.ps1").read_text(
            encoding="utf-8"
        )
        self.assertEqual("windows-x64", ownership["platform"])
        self.assertEqual(
            {
                "ProgramFiles/ProjectSEAM/ProjectSEAM.exe",
                "ProgramFiles/ProjectSEAM/Resources",
                "ProgramFiles/ProjectSEAM/RELEASE_IDENTITY.json",
                "ProgramFiles/ProjectSEAM/release-payload-manifest.json",
                "ProgramFiles/ProjectSEAM/release-dependency-closure.json",
                "ProgramFiles/ProjectSEAM/seam_installer_verifier.exe",
                "CommonFiles/CLAP/ProjectSEAMEditor.clap",
                "CommonFiles/CLAP/ProjectSEAMEditor.resources",
                "CommonFiles/VST3/ProjectSEAMEditor.vst3",
                "CommonAppData/ProjectSEAM/Documentation",
                "CommonAppData/ProjectSEAM/THIRD_PARTY_NOTICES.md",
                "CommonAppData/ProjectSEAM/SBOM.spdx.json",
                "CommonAppData/ProjectSEAM/Trust",
                "CommonAppData/ProjectSEAM/Ownership",
                "CommonAppData/ProjectSEAM/Notices",
                "ProgramFiles/ProjectSEAM/Uninstall.exe",
            },
            set(ownership["ownedPaths"]),
        )
        self.assertTrue(ownership["preservedUserRoots"])
        self.assertEqual(
            ["ProgramFiles/ProjectSEAM/InstallerReplay"],
            ownership["preservedSystemRoots"],
        )
        self.assertEqual(2, len(ownership["ownedShortcuts"]))
        self.assertEqual(1, len(ownership["ownedRegistryKeys"]))
        self.assertEqual(
            "separate-explicit-action", ownership["destructiveDataRemoval"]
        )
        self.assertIn("$PROGRAMFILES64\\ProjectSEAM", nsi)
        self.assertIn("$COMMONFILES64\\CLAP", nsi)
        self.assertIn("$COMMONFILES64\\VST3", nsi)
        self.assertIn("$APPDATA\\ProjectSEAM", nsi)
        self.assertNotIn("$COMMONAPPDATA", nsi)
        self.assertIn("Documentation\\external-beta-documentation.json", builder)
        self.assertIn("release-payload-manifest.json", builder)
        self.assertIn("Notices\\openssl-LICENSE.txt", builder)
        self.assertIn("Tools\\seam_installer_verifier.exe", builder)
        self.assertIn("verify_release_payload_manifest.py", builder)
        self.assertIn("Function .onInit", nsi)
        self.assertLess(
            nsi.index("Function .onInit"), nsi.index('Section "Project SEAM"')
        )
        self.assertIn("seam_installer_verifier.exe", nsi)
        self.assertIn("--expected-candidate", nsi)
        self.assertIn("--expected-handoff-sha256", nsi)
        self.assertIn("/HANDOFFSHA256=", nsi)
        for forbidden in ("--root-key", "--replay-root", "--now"):
            self.assertNotIn(forbidden, nsi)
        self.assertIn("!uninstfinalize", nsi)
        self.assertEqual(2, nsi.count("/SD IDOK"))
        self.assertIn("Standalone\\seam_editor_native.exe", signing)
        self.assertIn("Tools\\seam_installer_verifier.exe", signing)
        self.assertIn("$env:ProgramFiles\\ProjectSEAM\\Uninstall.exe", oracle)
        self.assertIn("signtool.exe verify /pa /all /v $uninstaller", oracle)
        self.assertIn("ownedPayloadRemoved", oracle)
        self.assertIn("replayStatePreserved", oracle)
        self.assertIn("$ownership.ownedPaths", oracle)
        self.assertIn("$ownership.ownedShortcuts", oracle)
        self.assertIn("$ownership.ownedRegistryKeys", oracle)
        self.assertIn("already consumed", oracle)
        self.assertNotIn("$LOCALAPPDATA", nsi)


if __name__ == "__main__":
    unittest.main()
