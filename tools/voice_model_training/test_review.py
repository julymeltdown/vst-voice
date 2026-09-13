import base64
import copy
import unittest
import hashlib
import json
import tempfile
import time
import subprocess
import sys
from pathlib import Path
from tools.phase13a.update_contract import ed25519_public_key, ed25519_sign
from tools.public_release.crypto_validation import canonical_signing_payload
from tools.public_release.contracts import sha256_json
from tools.voice_model_training.review import verify_training_review
from tools.voice_model_training.__main__ import admit_sources
from tools.voice_model_training.permissions import TRAINING_PERMISSIONS
from tools.voice_model_training.test_audio_source import wav


class ReviewTests(unittest.TestCase):
    def test_signed_review_identity_time_and_external_policy(self):
        seed = bytes(range(32))  # Test-only signer, never a product trust key.
        policy = dict(policyVersion="seam-training-review-1", algorithm="Ed25519", requiredRoles=["training-rights-reviewer"],
                      trustedKeys=[dict(keyId="fixture", role="training-rights-reviewer", signerId="fixture-reviewer",
                      publicKey=base64.b64encode(ed25519_public_key(seed)).decode())])
        anchor = sha256_json(policy)
        review = dict(formatId="com.project-seam.training-rights-review", schemaVersion=1,
                      policyVersion=policy["policyVersion"], policySha256=anchor, algorithm="Ed25519",
                      keyId="fixture", signerId="fixture-reviewer", configurationSha256="a" * 64,
                      decision="APPROVE_TRAINING_SCOPES", issuedAt=100, expiresAt=200)
        review["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(review, "recordSha256"), seed)).decode()
        review["recordSha256"] = sha256_json(review)
        def verify(value=review, **kwargs):
            args = dict(policy=policy, trusted_policy_sha256=anchor, configuration_sha256="a" * 64, now=150)
            args.update(kwargs)
            return verify_training_review(value, **args)
        self.assertTrue(verify()["reviewAuthenticated"])
        self.assertFalse(verify()["trainingAdmitted"])
        for key, value in (("signerId", "other"), ("decision", "REJECT"), ("signature", "A" * 88)):
            altered = dict(review, **{key: value})
            with self.assertRaises(ValueError): verify(altered)
        for args in (dict(now=200), dict(now=99), dict(configuration_sha256="b" * 64), dict(trusted_policy_sha256="0" * 64)):
            with self.assertRaises(ValueError): verify(**args)
        changed = copy.deepcopy(policy); changed["trustedKeys"][0]["role"] = "release-manager"
        with self.assertRaises(ValueError): verify(policy=changed)
        weak_policy = copy.deepcopy(policy)
        identity = bytes([1]) + bytes(31)
        weak_policy["trustedKeys"][0]["publicKey"] = base64.b64encode(identity).decode()
        weak_anchor = sha256_json(weak_policy)
        forged = dict(review, policySha256=weak_anchor, signature=base64.b64encode(identity + bytes(32)).decode())
        forged["recordSha256"] = sha256_json({k: v for k, v in forged.items() if k != "recordSha256"})
        with self.assertRaises(ValueError):
            verify(forged, policy=weak_policy, trusted_policy_sha256=weak_anchor)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio, evidence = wav(), b"test-only rights assertion"
            (root / "audio.wav").write_bytes(audio)
            (root / "rights.txt").write_bytes(evidence)
            source = dict(sourceId="source", sourceSha256=hashlib.sha256(audio).hexdigest(),
                          songId="song", sessionId="session", lineageId="lineage", path="audio.wav")
            permission = dict(sourceId="source", sourceSha256=source["sourceSha256"], identityId="singer",
                kind="PROCEDURAL_SYNTHESIS", evidenceId="rights", evidenceSha256=hashlib.sha256(evidence).hexdigest(),
                reviewRevision="fixture", permissions=dict.fromkeys(TRAINING_PERMISSIONS, True))
            config = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1, sampleRate=48000,
                sources=[source], evidence={"rights": "rights.txt"}, manifest=dict(
                    formatId="com.project-seam.training-permission-manifest", schemaVersion=1, sources=[permission]))
            def capture():
                payload = json.dumps(config).encode()
                (root / "config.json").write_bytes(payload)
                bound = dict(review, configurationSha256=hashlib.sha256(payload).hexdigest())
                bound["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(bound, "recordSha256"), seed)).decode()
                bound["recordSha256"] = sha256_json({k: v for k, v in bound.items() if k != "recordSha256"})
                return bound
            bound = capture()
            def admit():
                return admit_sources(root / "config.json", bound["configurationSha256"], root,
                    review=bound, policy=policy, trusted_policy_sha256=anchor, now=150)
            result = admit()
            self.assertTrue(result["sourcePermissionsAdmitted"])
            self.assertFalse(result["trainingAdmitted"])
            # Exercise the actual CLI clock with a fixture-only currently valid review.
            cli_review = dict(bound, issuedAt=int(time.time()) - 60, expiresAt=int(time.time()) + 600)
            cli_review["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(cli_review, "recordSha256"), seed)).decode()
            cli_review["recordSha256"] = sha256_json({k: v for k, v in cli_review.items() if k != "recordSha256"})
            review_bytes, policy_bytes = json.dumps(cli_review).encode(), json.dumps(policy).encode()
            (root / "review.json").write_bytes(review_bytes)
            (root / "policy.json").write_bytes(policy_bytes)
            command = [sys.executable, "-m", "tools.voice_model_training", "admit", str(root / "config.json"),
                bound["configurationSha256"], str(root), str(root / "admission.json"), "--review", str(root / "review.json"),
                "--review-sha256", hashlib.sha256(review_bytes).hexdigest(), "--policy", str(root / "policy.json"),
                "--policy-file-sha256", hashlib.sha256(policy_bytes).hexdigest(), "--trusted-policy-sha256", anchor]
            self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 0)
            published = (root / "admission.json").read_bytes()
            self.assertTrue(json.loads(published)["sourcePermissionsAdmitted"])
            self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 2)
            self.assertEqual((root / "admission.json").read_bytes(), published)
            command[7] = str(root / "wrong-anchor.json")
            command[-1] = "0" * 64
            self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 2)
            self.assertFalse((root / "wrong-anchor.json").exists())
            permission["permissions"]["modelTraining"] = False
            bound = capture()
            with self.assertRaises(ValueError): admit()
            permission["permissions"]["modelTraining"] = True
            bound = capture()
            (root / "audio.wav").write_bytes(audio[:-1])
            with self.assertRaises(ValueError): admit()


if __name__ == "__main__":
    unittest.main()
