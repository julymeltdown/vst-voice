from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.phase13a.signing_eligibility import (
    production_payload_issues,
    production_source_trust_issues,
)

ROOT = Path(__file__).resolve().parents[2]


class SigningEligibilityTests(unittest.TestCase):
    def test_u55_inputs_block_production_credentials(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trust = root / "packaging/trust/release-trust-roots.json"
            trust.parent.mkdir(parents=True)
            trust.write_text(json.dumps({"testOnly": True}), encoding="utf-8")
            payload_trust = root / "payload/Trust/release-trust-roots.json"
            payload_trust.parent.mkdir(parents=True)
            payload_trust.write_text(json.dumps({"testOnly": True}), encoding="utf-8")
            (root / "payload/release-payload-manifest.json").write_text(
                json.dumps({"releaseEligible": False, "developmentTrustOnly": True}),
                encoding="utf-8",
            )

            self.assertTrue(production_source_trust_issues(root))
            self.assertTrue(production_payload_issues(root / "payload"))

    def test_eligibility_gate_precedes_every_production_credential_read(self) -> None:
        macos = (ROOT / "scripts/sign_macos_plugin_payload.sh").read_text(
            encoding="utf-8"
        )
        windows = (ROOT / "scripts/sign_windows_payload.ps1").read_text(
            encoding="utf-8"
        )
        macos_installer = (ROOT / "scripts/package_macos_plugins.sh").read_text(
            encoding="utf-8"
        )
        macos_standalone = (ROOT / "scripts/package_macos_standalone.sh").read_text(
            encoding="utf-8"
        )
        windows_builder = (ROOT / "scripts/build_windows_installer.ps1").read_text(
            encoding="utf-8"
        )
        windows_file = (ROOT / "scripts/sign_windows_file.ps1").read_text(
            encoding="utf-8"
        )
        windows_installer = (ROOT / "scripts/sign_windows_installer.ps1").read_text(
            encoding="utf-8"
        )
        workflow = (ROOT / ".github/workflows/phase13a-distribution.yml").read_text(
            encoding="utf-8"
        )
        gate = "verify_production_signing_input.py"
        self.assertLess(
            macos.index(gate), macos.index("APPLE_DEVELOPER_ID_APPLICATION")
        )
        self.assertLess(windows.index(gate), windows.index("WINDOWS_SIGN_CERT_SHA1"))
        self.assertLess(
            macos_installer.index(gate),
            macos_installer.index("APPLE_DEVELOPER_ID_INSTALLER"),
        )
        self.assertLess(
            macos_standalone.index(gate),
            macos_standalone.index("APPLE_DEVELOPER_ID_APPLICATION"),
        )
        self.assertIn("[switch]$SignUninstaller", windows_builder)
        self.assertIn("[switch]$DevelopmentSignUninstaller", windows_builder)
        self.assertIn("WINDOWS_DEVELOPMENT_SIGN_CERT_SHA1", windows_file)
        self.assertLess(
            windows_builder.index(gate), windows_builder.index("sign_windows_file.ps1")
        )
        self.assertLess(
            windows_file.index(gate), windows_file.index("WINDOWS_SIGN_CERT_SHA1")
        )
        self.assertLess(
            windows_installer.index(gate),
            windows_installer.index("WINDOWS_SIGN_CERT_SHA1"),
        )
        self.assertIn("-SignUninstaller", workflow)
        self.assertIn("sign_windows_installer.ps1 -PayloadRoot", workflow)
        self.assertLess(workflow.index(gate), workflow.index("WINDOWS_SIGN_PFX_BASE64"))
        self.assertLess(
            workflow.rindex(gate),
            workflow.index("APPLE_DEVELOPER_ID_APPLICATION_P12"),
        )


if __name__ == "__main__":
    unittest.main()
