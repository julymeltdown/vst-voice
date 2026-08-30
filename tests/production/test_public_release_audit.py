from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from tests.production.public_release_archive_fixtures import archived_candidate
from tests.production.public_release_fixtures import acceptance_contract, candidate


class PublicReleaseAuditTests(unittest.TestCase):
    def test_archive_audit_requires_every_referenced_raw_record(self) -> None:
        from tools.public_release.release_audit import audit_release

        contract = acceptance_contract()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            value, manifest = archived_candidate(root, candidate(contract))

            result = audit_release(
                value,
                manifest,
                root,
                "PUBLIC_ACTIVE",
                acceptance_contract=contract,
            )

            self.assertTrue(result.passed, result.errors)
            records = value["evidence"]
            assert isinstance(records, list)
            raw = records[0]["rawArchive"]
            assert isinstance(raw, dict)
            (root / str(raw["path"])).unlink()
            missing = audit_release(
                value,
                manifest,
                root,
                "PUBLIC_ACTIVE",
                acceptance_contract=contract,
            )
            self.assertFalse(missing.passed)
            self.assertTrue(any("restore" in error.lower() for error in missing.errors))


if __name__ == "__main__":
    unittest.main()
