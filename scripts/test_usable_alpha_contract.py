#!/usr/bin/env python3
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from verify_usable_alpha_contract import verify_repository


class UsableAlphaContractVerificationTests(unittest.TestCase):
    def make_repo(self, *, readme: str, gate_status: str = "BLOCKED", pass_without_evidence: bool = False) -> Path:
        temp = Path(tempfile.mkdtemp(prefix="seam-usable-alpha-contract-"))
        (temp / "docs" / "product").mkdir(parents=True)
        (temp / "README.md").write_text(readme, encoding="utf-8")
        requirements = []
        for index in range(1, 21):
            status = "PASS" if pass_without_evidence and index == 1 else "NOT_RUN"
            requirements.append({
                "id": f"UA-{index:03d}",
                "mandatory": True,
                "status": status,
                "evidence": [],
            })
        payload = {
            "schemaVersion": 1,
            "gate": {"name": "Usable Alpha", "status": gate_status},
            "requirements": requirements,
        }
        (temp / "docs" / "product" / "usable-alpha-acceptance.json").write_text(
            json.dumps(payload, indent=2), encoding="utf-8"
        )
        return temp

    def test_rejects_readme_without_canonical_contract_link(self) -> None:
        root = self.make_repo(readme="# Project SEAM\n")
        errors = verify_repository(root)
        self.assertTrue(any("README" in error for error in errors), errors)

    def test_rejects_pass_without_evidence_path_and_sha256(self) -> None:
        root = self.make_repo(
            readme="[Usable Alpha](docs/product/USABLE_ALPHA_ACCEPTANCE.md)\n",
            pass_without_evidence=True,
        )
        errors = verify_repository(root)
        self.assertTrue(any("UA-001" in error and "evidence" in error for error in errors), errors)

    def test_rejects_passed_gate_when_a_mandatory_requirement_is_not_pass(self) -> None:
        root = self.make_repo(
            readme="[Usable Alpha](docs/product/USABLE_ALPHA_ACCEPTANCE.md)\n",
            gate_status="PASSED",
        )
        errors = verify_repository(root)
        self.assertTrue(any("gate" in error.lower() and "mandatory" in error.lower() for error in errors), errors)


class RepositoryUsableAlphaContractTests(unittest.TestCase):
    def test_checked_in_contract_is_valid(self) -> None:
        root = Path(__file__).resolve().parents[1]
        self.assertEqual(verify_repository(root), [])


if __name__ == "__main__":
    unittest.main()
