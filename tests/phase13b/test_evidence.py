import hashlib
import os
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13b"))

import evidence  # noqa: E402


class EvidenceTests(unittest.TestCase):
    def test_valid_evidence_requires_matching_hash_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "evidence" / "contract.pdf"
            path.parent.mkdir()
            path.write_bytes(b"signed-contract")
            record = {
                "path": "evidence/contract.pdf",
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "kind": "signed-contract",
                "executedAt": "2026-08-18T12:00:00Z",
                "reviewer": "legal-reviewer",
            }
            self.assertEqual([], evidence.validate_evidence_record(record, root))

    def test_path_traversal_symlink_and_hash_mismatch_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outside = root.parent / (root.name + "-outside.txt")
            outside.write_bytes(b"outside")
            try:
                (root / "link.txt").symlink_to(outside)
                records = [
                    {"path": "../outside.txt", "sha256": "0" * 64, "kind": "x", "executedAt": "x", "reviewer": "x"},
                    {"path": "link.txt", "sha256": hashlib.sha256(outside.read_bytes()).hexdigest(), "kind": "x", "executedAt": "x", "reviewer": "x"},
                ]
                errors = [evidence.validate_evidence_record(record, root) for record in records]
                self.assertTrue(any("safe relative" in item for item in errors[0]))
                self.assertTrue(any("symbolic" in item for item in errors[1]))
            finally:
                outside.unlink(missing_ok=True)

    def test_evidence_file_size_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "large.bin"
            path.write_bytes(b"1234")
            record = {
                "path": "large.bin",
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "kind": "log",
                "executedAt": "2026-08-18T12:00:00Z",
                "reviewer": "qa",
            }
            errors = evidence.validate_evidence_record(record, root, maximum_bytes=3)
            self.assertTrue(any("maximum" in item for item in errors))


if __name__ == "__main__":
    unittest.main()
