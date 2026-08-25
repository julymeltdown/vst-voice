from __future__ import annotations

import copy
import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.predecessor_release import STATE_FAMILIES, seal_predecessor, validate_predecessor


def _digest(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _record(root: Path) -> dict:
    fixtures = []
    for family in STATE_FAMILIES:
        path = root / f"fixtures/{family}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(family, encoding="utf-8")
        fixtures.append({
            "family": family,
            "archivePath": str(path.relative_to(root)),
            "beforeSha256": _digest(f"before-{family}"),
            "afterSha256": _digest(f"after-{family}"),
            "archiveSha256": _digest(f"archive-{family}"),
        })
    return {
        "schemaVersion": 1,
        "status": "SIGNED_COHERENT",
        "releaseIdentity": {"version": "0.13.0", "buildId": "predecessor-001", "sourceCommit": "a" * 40},
        "bankSha256": _digest("bank"),
        "trustPolicySha256": _digest("trust"),
        "documentationSha256": _digest("docs"),
        "archiveSha256": _digest("archive"),
        "packages": {
            "macos": {"status": "SIGNED", "packageSha256": _digest("mac-package"), "installedTreeSha256": _digest("mac-tree"), "notarizedStapled": True},
            "windows": {"status": "SIGNED", "packageSha256": _digest("win-package"), "installedTreeSha256": _digest("win-tree"), "timestamped": True},
        },
        "stateFixtures": fixtures,
    }


class PredecessorReleaseTests(unittest.TestCase):
    def test_complete_predecessor_seals_after_archive_restore(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            self.assertEqual([], validate_predecessor(record, root))
            sealed = seal_predecessor(record, root)
            self.assertEqual(64, len(sealed["recordSha256"]))

    def test_missing_fixture_or_unsigned_platform_blocks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            changed = copy.deepcopy(record)
            changed["packages"]["windows"]["timestamped"] = False
            changed["stateFixtures"] = changed["stateFixtures"][:-1]
            errors = validate_predecessor(changed, root)
            self.assertTrue(any("timestamped" in error for error in errors))
            self.assertTrue(any("persistent state family" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
