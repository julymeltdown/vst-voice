from __future__ import annotations

import unittest

from tests.production.public_release_contract_fixtures import JsonObject
from tests.production.public_release_fixtures import candidate
from tools.public_release.evidence_validation import evidence_record_findings


def support_record(value: JsonObject) -> JsonObject:
    records = value["evidence"]
    assert isinstance(records, list)
    for record in records:
        if isinstance(record, dict) and record.get("requirementId") == "PR-010-support-intake":
            return record
    raise AssertionError("fixture must contain support evidence")


class PublicSupportEvidenceTests(unittest.TestCase):
    def test_missing_bundle_identity_cannot_satisfy_support_evidence(self) -> None:
        value = candidate()
        support_record(value).pop("supportBundleSha256", None)

        findings = evidence_record_findings(value)

        self.assertIn("PR-010-support-intake", {item.requirement_id for item in findings})

    def test_different_bundle_cannot_satisfy_support_evidence(self) -> None:
        value = candidate()
        support_record(value)["supportBundleSha256"] = "e" * 64

        findings = evidence_record_findings(value)

        self.assertIn("PR-010-support-intake", {item.requirement_id for item in findings})

    def test_matching_bundle_retains_valid_evidence(self) -> None:
        value = candidate()
        support_record(value)["supportBundleSha256"] = "f" * 64

        findings = evidence_record_findings(value)

        self.assertEqual((), findings)


if __name__ == "__main__":
    unittest.main()
