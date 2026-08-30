from __future__ import annotations

import base64
import json
import tempfile
import unittest
from datetime import UTC, datetime
from pathlib import Path

from tools.phase13a import update_contract
from tools.phase13a.development_handoff import create_development_update_contract
from tools.phase13a.release_payload import PayloadPlatform


class DevelopmentInstallerHandoffTests(unittest.TestCase):
    def test_contract_is_signed_and_bound_to_exact_installer_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "ProjectSEAM-0.13.1.pkg"
            package.write_bytes(b"developer installer")
            result = create_development_update_contract(
                package,
                PayloadPlatform.MACOS_ARM64,
                root / "contract",
                datetime(2026, 8, 31, 12, tzinfo=UTC),
            )
            second = create_development_update_contract(
                package,
                PayloadPlatform.MACOS_ARM64,
                root / "contract-second",
                datetime(2026, 8, 31, 12, tzinfo=UTC),
            )

            policy = json.loads(result.policy.read_text(encoding="utf-8"))
            manifest = json.loads(result.manifest.read_text(encoding="utf-8"))
            second_manifest = json.loads(second.manifest.read_text(encoding="utf-8"))
            root_key = json.loads(result.root_key.read_text(encoding="utf-8"))
            trusted = base64.b64decode(policy["rootPublicKey"])
            self.assertEqual(
                [],
                update_contract.verify_update_manifest(
                    manifest,
                    policy,
                    now=datetime(2026, 8, 31, 12, tzinfo=UTC),
                    trusted_policy_roots={policy["rootKeyId"]: trusted},
                ),
            )
            self.assertTrue(result.test_only)
            self.assertNotEqual(manifest["manifestId"], second_manifest["manifestId"])
            self.assertEqual(policy["rootKeyId"], root_key["keyId"])
            self.assertEqual(
                update_contract.sha256_file(package), manifest["package"]["sha256"]
            )


if __name__ == "__main__":
    unittest.main()
