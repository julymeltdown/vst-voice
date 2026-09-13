import base64
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import update_contract  # noqa: E402


class UpdateCliTests(unittest.TestCase):
    def test_rfc8032_known_answers(self):
        # RFC 8032 section 7.1, test vectors 1 and 2; public test keys only.
        # https://www.rfc-editor.org/rfc/rfc8032.html#section-7.1
        vectors = [
            ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
             "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
             "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
             "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
            ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
             "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
             "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
             "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")]
        for seed_hex, key_hex, message_hex, signature_hex in vectors:
            seed, key, message, signature = map(bytes.fromhex, (seed_hex, key_hex, message_hex, signature_hex))
            self.assertEqual(update_contract.ed25519_public_key(seed), key)
            self.assertEqual(update_contract.ed25519_sign(message, seed), signature)
            self.assertTrue(update_contract.ed25519_verify(signature, message, key))
            malleable_s = (int.from_bytes(signature[32:], "little") + update_contract._L).to_bytes(32, "little")
            self.assertFalse(update_contract.ed25519_verify(signature[:32] + malleable_s, message, key))

    def test_weak_key_forgery_and_invalid_points_reject(self):
        identity = bytes([1]) + bytes(31)
        forged = identity + bytes(32)
        self.assertFalse(update_contract.ed25519_verify(forged, b"arbitrary approval", identity))
        sign_alias = identity[:31] + bytes([128])
        self.assertIsNone(update_contract._decode_point(sign_alias))
        self.assertIsNone(update_contract._decode_point((2**255 - 19).to_bytes(32, "little")))
        for y in range(20):
            encoded = y.to_bytes(32, "little")
            point = update_contract._decode_point(encoded)
            if point is not None:
                x, decoded_y = point
                self.assertEqual((-x*x + decoded_y*decoded_y - 1 - update_contract._D*x*x*decoded_y*decoded_y) % update_contract._Q, 0)
        seed, message = bytes(range(32)), b"valid ordinary signature"
        key = update_contract.ed25519_public_key(seed)
        signature = update_contract.ed25519_sign(message, seed)
        self.assertTrue(update_contract.ed25519_verify(signature, message, key))
        self.assertFalse(update_contract.ed25519_verify(signature, message + b"changed", key))

    def test_release_signing_requires_external_signer_and_public_key(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "input.json"
            output_path = root / "output.json"
            input_path.write_text('{"schemaVersion":1}\n', encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/sign_update_manifest.py"), "--input", str(input_path), "--output", str(output_path), "--key-id", "release"], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("signer-command", result.stderr)

    def test_test_only_seed_signature_is_verifiable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "input.json"
            output_path = root / "output.json"
            input_path.write_text('{"schemaVersion":1,"purpose":"update-manifest"}\n', encoding="utf-8")
            seed = bytes.fromhex("22" * 32)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/sign_update_manifest.py"), "--input", str(input_path), "--output", str(output_path), "--key-id", "test-key", "--test-seed-hex", seed.hex(), "--test-only"], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(0, result.returncode, result.stderr)
            value = json.loads(output_path.read_text(encoding="utf-8"))
            signature = base64.b64decode(value["signature"]["value"])
            self.assertTrue(update_contract.ed25519_verify(signature, update_contract.canonical_json(value), update_contract.ed25519_public_key(seed)))


if __name__ == "__main__":
    unittest.main()
