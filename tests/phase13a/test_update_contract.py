import base64
import hashlib
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

import update_contract as contract  # noqa: E402


class UpdateContractTests(unittest.TestCase):
    now = datetime(2026, 8, 21, 12, tzinfo=timezone.utc)

    def policy_and_manifest(self):
        root_seed = bytes.fromhex("00" * 31 + "01")
        update_seed = bytes.fromhex("11" * 32)
        root_public = contract.ed25519_public_key(root_seed)
        update_public = contract.ed25519_public_key(update_seed)
        policy = {
            "schemaVersion": 1,
            "purpose": "update-trust-policy",
            "channel": "external-beta",
            "policyEpoch": 4,
            "rootKeyId": "root-2026",
            "rootPublicKey": base64.b64encode(root_public).decode(),
            "allowedPlatforms": ["macos-arm64", "windows-x64"],
            "issuedAt": "2026-08-21T00:00:00Z",
            "notBefore": "2026-08-21T00:00:00Z",
            "expiresAt": "2026-09-21T00:00:00Z",
            "compromiseCutoff": "2026-09-20T00:00:00Z",
            "delegatedKeys": [{
                "keyId": "update-2026",
                "purpose": "update",
                "algorithm": "Ed25519",
                "publicKey": base64.b64encode(update_public).decode(),
                "notBefore": "2026-08-21T00:00:00Z",
                "expiresAt": "2026-09-20T00:00:00Z",
            }],
        }
        policy_payload = contract.canonical_json(policy)
        policy["signature"] = {
            "algorithm": "Ed25519",
            "keyId": "root-2026",
            "payloadSha256": hashlib.sha256(policy_payload).hexdigest(),
            "value": base64.b64encode(contract.ed25519_sign(policy_payload, root_seed)).decode(),
        }
        package_bytes = b"signed candidate bytes"
        manifest = {
            "schemaVersion": 1,
            "purpose": "update-manifest",
            "channel": "external-beta",
            "manifestId": "candidate-2026-08-21",
            "manifestEpoch": 5,
            "platform": "macos-arm64",
            "targetBuild": "external-beta.20260821.1",
            "targetVersion": "0.14.0",
            "minimumVersion": "0.13.0",
            "issuedAt": "2026-08-21T00:00:00Z",
            "expiresAt": "2026-09-20T00:00:00Z",
            "readRanges": {name: {"min": 1, "max": 6} for name in contract.RANGE_FIELDS},
            "writeRanges": {name: {"min": 6, "max": 6} for name in contract.RANGE_FIELDS},
            "downgradePolicy": "REJECT",
            "releaseNotesSha256": "a" * 64,
            "package": {
                "fileName": "ProjectSEAM-0.14.0-macos-arm64.pkg",
                "url": "https://updates.example.invalid/project-seam.pkg",
                "size": len(package_bytes),
                "sha256": hashlib.sha256(package_bytes).hexdigest(),
            },
        }
        manifest_payload = contract.canonical_json(manifest)
        manifest["signature"] = {
            "algorithm": "Ed25519",
            "keyId": "update-2026",
            "payloadSha256": hashlib.sha256(manifest_payload).hexdigest(),
            "value": base64.b64encode(contract.ed25519_sign(manifest_payload, update_seed)).decode(),
        }
        return root_public, policy, manifest, package_bytes

    def test_signed_policy_and_manifest_verify(self):
        root_public, policy, manifest, _ = self.policy_and_manifest()
        self.assertEqual([], contract.verify_trust_policy(policy, {"root-2026": root_public}, now=self.now))
        self.assertEqual([], contract.verify_update_manifest(manifest, policy, installed_version="0.13.0", now=self.now))

    def test_replay_and_downgrade_are_rejected(self):
        _, policy, manifest, _ = self.policy_and_manifest()
        errors = contract.validate_update_manifest({**manifest, "targetVersion": "0.12.0"}, policy, installed_version="0.13.0", now=self.now)
        self.assertTrue(any("downgrade" in error for error in errors))
        state, errors = contract.accept_manifest(manifest, {"highestManifestEpoch": 5})
        self.assertIsNone(state)
        self.assertTrue(any("stale" in error for error in errors))

    def test_handoff_revalidates_exact_candidate_bytes(self):
        _, policy, manifest, package_bytes = self.policy_and_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.pkg"
            source.write_bytes(package_bytes)
            staging = root / "staging"
            handoff = contract.stage_verified_package(source, manifest, staging)
            self.assertEqual([], contract.verify_sealed_handoff(handoff, manifest, staging))
            candidate = staging / handoff["package"]["relativePath"]
            candidate.write_bytes(b"replacement")
            self.assertTrue(contract.verify_sealed_handoff(handoff, manifest, staging))


if __name__ == "__main__":
    unittest.main()
