from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.evidence_archive import create_archive_manifest, validate_archive_manifest


class EvidenceArchiveTests(unittest.TestCase):
    def test_archive_rejects_local_anchor_locators(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "archive" / "record.json"
            path.parent.mkdir()
            path.write_text("record", encoding="utf-8")
            with self.assertRaises(ValueError):
                create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="file:///tmp/self-authored-anchor")
            for locator in ("https://localhost/archive", "https://127.0.0.1/archive"):
                with self.subTest(locator=locator):
                    with self.assertRaises(ValueError):
                        create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator=locator)

    def test_sealed_archive_manifest_binds_every_raw_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "archive" / "record.json"
            path.parent.mkdir(parents=True)
            path.write_bytes(b"raw-record")
            manifest = create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="https://evidence.example/archive-001")
            self.assertEqual([], validate_archive_manifest(manifest, root))
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), manifest["entries"][0]["sha256"])

    def test_archive_tamper_and_manifest_mutation_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "archive" / "record.json"
            path.parent.mkdir(parents=True)
            path.write_bytes(b"raw-record")
            manifest = create_archive_manifest("candidate-root-001", root, ["archive/record.json"], "archive-001", anchor_locator="https://evidence.example/archive-001")
            path.write_bytes(b"tampered")
            self.assertTrue(any("sha256" in error for error in validate_archive_manifest(manifest, root)))
            path.write_bytes(b"raw-record")
            manifest["candidateRootId"] = "other-root"
            self.assertTrue(any("manifestSha256" in error for error in validate_archive_manifest(manifest, root)))

    def test_archive_rejects_symlinked_raw_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "private.log"
            target.write_bytes(b"private")
            link = root / "archive" / "record.json"
            link.parent.mkdir(parents=True)
            link.symlink_to(target)
            manifest = {
                "schemaVersion": 1,
                "recordType": "external-beta-evidence-archive",
                "archiveId": "archive-001",
                "candidateRootId": "candidate-root-001",
                "status": "SEALED",
                "anchored": True,
                "immutable": True,
                "anchor": {"kind": "external", "locator": "archive://001", "sha256": "a" * 64},
                "createdAt": "2026-08-22T12:00:00Z",
                "roles": {"producer": "A6", "reviewer": "A4"},
                "entries": [{"path": "archive/record.json", "kind": "evidence-record", "privacyClass": "PUBLIC_TECHNICAL", "sha256": "b" * 64, "size": 7}],
                "manifestSha256": "c" * 64,
            }
            self.assertTrue(any("symbolic" in error for error in validate_archive_manifest(manifest, root)))


if __name__ == "__main__":
    unittest.main()
