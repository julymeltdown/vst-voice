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
