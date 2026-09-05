from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.external_beta.full_product_contract import full_product_contract_errors


class FullProductReaderTests(unittest.TestCase):
    def errors_for(self, payload: bytes) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "contract.json"
            path.write_bytes(payload)
            return full_product_contract_errors({"fullProductContract": {
                "locator": str(path),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }})

    def test_large_contract_is_rejected_before_definition_parsing(self) -> None:
        errors = self.errors_for(b"{}" + b" " * (1024 * 1024))
        self.assertTrue(any("size limit" in error for error in errors), errors)

    def test_duplicate_contract_keys_are_rejected(self) -> None:
        errors = self.errors_for(b'{"schemaVersion":1,"schemaVersion":1}')
        self.assertTrue(any("duplicate" in error for error in errors), errors)

    def test_deep_json_returns_a_diagnostic_instead_of_raising(self) -> None:
        errors = self.errors_for(b'[' * 2000 + b'0' + b']' * 2000)
        self.assertTrue(any("invalid JSON" in error for error in errors), errors)

    def test_nonfinite_json_is_rejected(self) -> None:
        for value in (b"NaN", b"Infinity", b"-Infinity", b"1e309", b"-1e309"):
            with self.subTest(value=value):
                errors = self.errors_for(b'{"value":' + value + b'}')
                self.assertTrue(any("invalid JSON" in error for error in errors), errors)

    def test_regular_small_contract_reaches_definition_validation(self) -> None:
        for payload in (b'{}', b'{}' + b' ' * (1024 * 1024 - 2)):
            with self.subTest(size=len(payload)):
                errors = self.errors_for(payload)
                self.assertTrue(any("full-product requirements" in error for error in errors), errors)
                self.assertFalse(any("cannot be read" in error for error in errors), errors)

    @unittest.skipUnless(hasattr(os, "mkfifo"), "POSIX FIFO behavior")
    def test_fifo_is_rejected_without_waiting_for_a_writer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "contract.pipe"
            os.mkfifo(path)
            reference = json.dumps({"fullProductContract": {
                "locator": str(path), "sha256": "0" * 64,
            }})
            result = subprocess.run(
                (sys.executable, "-c",
                 "import json,sys; from tools.external_beta.full_product_contract "
                 "import full_product_contract_errors; "
                 "print(json.dumps(full_product_contract_errors(json.loads(sys.argv[1]))))",
                 reference),
                capture_output=True, text=True, timeout=3, check=False,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("regular file", result.stdout)


if __name__ == "__main__":
    unittest.main()
