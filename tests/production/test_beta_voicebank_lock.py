"""The beta voicebank lock is a release identity: it binds a candidate, its package, its
inventory and one canonical song so a later run can prove it is looking at the same bytes.

It had no test at all. This covers the whole contract, and in particular the one field that
was recorded but never checked: `sourceDerivedTreeSha256` was in the required set and was
written from the candidate's source and derived assets, but validate never compared it. A lock
whose recorded tree hash had been altered validated clean, which is precisely the tampering the
file exists to detect.
"""
from __future__ import annotations

import copy
import unittest

from tools.external_beta.voicebank_production import (
    create_beta_lock,
    validate_beta_lock,
)


CANDIDATE = {
    "format": "com.project-seam.resource-candidate",
    "schemaVersion": 2,
    "resourceKind": "sample-procedural",
    "sourceAssets": ["source-a"],
    "derivedAssets": ["derived-a"],
}
PACKAGE = {
    "id": "beta-voicebank-01",
    "version": "1.0.0",
    "contentSha256": "b" * 64,
    "entryManifestSha256": "c" * 64,
}
INVENTORY = {"inventorySha256": "d" * 64}
SONG = {"projectSha256": "e" * 64, "mediaSha256": "f" * 64}
GENERATED_AT = "2026-10-03T00:00:00Z"


class BetaVoicebankLockTests(unittest.TestCase):
    def lock(self) -> dict:
        return create_beta_lock(CANDIDATE, PACKAGE, SONG, INVENTORY, generated_at=GENERATED_AT)

    def test_a_fresh_lock_validates(self) -> None:
        result = validate_beta_lock(self.lock(), CANDIDATE, PACKAGE, INVENTORY, SONG)
        self.assertTrue(result.passed, result.errors)

    def test_a_tampered_source_derived_tree_hash_is_refused(self) -> None:
        # The recorded hash is the lock's own statement about the candidate's source and derived
        # trees. If that statement is altered the lock no longer describes what it locked, even
        # though every other field still matches.
        lock = self.lock()
        self.assertTrue(lock["sourceDerivedTreeSha256"])
        lock["sourceDerivedTreeSha256"] = "0" * 64
        result = validate_beta_lock(lock, CANDIDATE, PACKAGE, INVENTORY, SONG)
        self.assertFalse(result.passed)
        self.assertTrue(any("sourceDerivedTreeSha256" in error for error in result.errors), result.errors)

    def test_changing_a_source_or_derived_asset_is_refused(self) -> None:
        for field, value in (("sourceAssets", ["source-b"]), ("derivedAssets", ["derived-b"])):
            candidate = copy.deepcopy(CANDIDATE)
            candidate[field] = value
            result = validate_beta_lock(self.lock(), candidate, PACKAGE, INVENTORY, SONG)
            self.assertFalse(result.passed, field)

    def test_every_bound_identity_is_checked(self) -> None:
        cases = {
            "candidateSha256": ("candidate", {"manifestSha256": "9" * 64}),
            "packageSha256": ("package", {"contentSha256": "9" * 64}),
            "entryManifestSha256": ("package", {"entryManifestSha256": "9" * 64}),
            "inventorySha256": ("inventory", {"inventorySha256": "9" * 64}),
            "canonicalSong": ("song", {"projectSha256": "9" * 64, "mediaSha256": "f" * 64}),
        }
        for field, (target, replacement) in cases.items():
            with self.subTest(field=field):
                changed = {"candidate": copy.deepcopy(CANDIDATE), "package": copy.deepcopy(PACKAGE),
                           "inventory": copy.deepcopy(INVENTORY), "song": copy.deepcopy(SONG)}
                changed[target].update(replacement)
                result = validate_beta_lock(self.lock(), changed["candidate"], changed["package"],
                                           changed["inventory"], changed["song"])
                self.assertFalse(result.passed)
                self.assertTrue(any(field in error for error in result.errors), (field, result.errors))

    def test_missing_fields_and_a_bad_timestamp_are_refused(self) -> None:
        lock = self.lock()
        del lock["canonicalSong"]
        result = validate_beta_lock(lock, CANDIDATE, PACKAGE, INVENTORY, SONG)
        self.assertFalse(result.passed)
        self.assertTrue(any("canonicalSong is required" in error for error in result.errors), result.errors)
        stale = self.lock()
        stale["generatedAt"] = "not-a-timestamp"
        self.assertFalse(validate_beta_lock(stale, CANDIDATE, PACKAGE, INVENTORY, SONG).passed)
        unlocked = self.lock()
        unlocked["status"] = "DRAFT"
        self.assertFalse(validate_beta_lock(unlocked, CANDIDATE, PACKAGE, INVENTORY, SONG).passed)


if __name__ == "__main__":
    unittest.main()
